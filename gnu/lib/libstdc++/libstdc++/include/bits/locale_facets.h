// Locale support -*- C++ -*-

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
// ISO C++ 14882: 22.1  Locales
//

/** @file locale_facets.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BITS_LOCFACETS_H
#define _CPP_BITS_LOCFACETS_H	1

#pragma GCC system_header

#include <ctime>	// For struct tm
#include <cwctype>	// For wctype_t
#include <iosfwd>
#include <bits/ios_base.h>  // For ios_base, ios_base::iostate
#include <streambuf>

namespace std
{
  // NB: Don't instantiate required wchar_t facets if no wchar_t support.
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
# define  _GLIBCPP_NUM_FACETS 28
#else
# define  _GLIBCPP_NUM_FACETS 14
#endif

  // Convert string to numeric value of type _Tv and store results.  
  // NB: This is specialized for all required types, there is no
  // generic definition.
  template<typename _Tv>
    void
    __convert_to_v(const char* __in, _Tv& __out, ios_base::iostate& __err, 
		   const __c_locale& __cloc, int __base = 10);

  // Explicit specializations for required types.
  template<>
    void
    __convert_to_v(const char*, long&, ios_base::iostate&, 
		   const __c_locale&, int);

  template<>
    void
    __convert_to_v(const char*, unsigned long&, ios_base::iostate&, 
		   const __c_locale&, int);

#ifdef _GLIBCPP_USE_LONG_LONG
  template<>
    void
    __convert_to_v(const char*, long long&, ios_base::iostate&, 
		   const __c_locale&, int);

  template<>
    void
    __convert_to_v(const char*, unsigned long long&, ios_base::iostate&, 
		   const __c_locale&, int);
#endif

  template<>
    void
    __convert_to_v(const char*, float&, ios_base::iostate&, 
		   const __c_locale&, int);

  template<>
    void
    __convert_to_v(const char*, double&, ios_base::iostate&, 
		   const __c_locale&, int);

 template<>
    void
    __convert_to_v(const char*, long double&, ios_base::iostate&, 
		   const __c_locale&, int);

  // NB: __pad is a struct, rather than a function, so it can be
  // partially-specialized.
  template<typename _CharT, typename _Traits>
    struct __pad
    {
      static void
      _S_pad(ios_base& __io, _CharT __fill, _CharT* __news, 
	     const _CharT* __olds, const streamsize __newlen, 
	     const streamsize __oldlen, const bool __num);
    };

  // Used by both numeric and monetary facets.
  // Check to make sure that the __grouping_tmp string constructed in
  // money_get or num_get matches the canonical grouping for a given
  // locale.
  // __grouping_tmp is parsed L to R
  // 1,222,444 == __grouping_tmp of "\1\3\3"
  // __grouping is parsed R to L
  // 1,222,444 == __grouping of "\3" == "\3\3\3"
  template<typename _CharT>
    bool
    __verify_grouping(const basic_string<_CharT>& __grouping, 
		      basic_string<_CharT>& __grouping_tmp);

  // Used by both numeric and monetary facets.
  // Inserts "group separator" characters into an array of characters.
  // It's recursive, one iteration per group.  It moves the characters
  // in the buffer this way: "xxxx12345" -> "12,345xxx".  Call this
  // only with __gbeg != __gend.
  template<typename _CharT>
    _CharT*
    __add_grouping(_CharT* __s, _CharT __sep,  
		   const char* __gbeg, const char* __gend, 
		   const _CharT* __first, const _CharT* __last);

  // This template permits specializing facet output code for
  // ostreambuf_iterator.  For ostreambuf_iterator, sputn is
  // significantly more efficient than incrementing iterators.
  template<typename _CharT>
    inline
    ostreambuf_iterator<_CharT>
    __write(ostreambuf_iterator<_CharT> __s, const _CharT* __ws, int __len)
    {
      __s._M_put(__ws, __len);
      return __s;
    }

  // This is the unspecialized form of the template.
  template<typename _CharT, typename _OutIter>
    inline
    _OutIter
    __write(_OutIter __s, const _CharT* __ws, int __len)
    {
      for (int __j = 0; __j < __len; __j++, ++__s)
	*__s = __ws[__j];
      return __s;
    }

  // 22.2.1.1  Template class ctype
  // Include host and configuration specific ctype enums for ctype_base.
  #include <bits/ctype_base.h>

  // Common base for ctype<_CharT>.  
  template<typename _CharT>
    class __ctype_abstract_base : public locale::facet, public ctype_base
    {
    public:
      // Types:
      typedef _CharT char_type;

      bool 
      is(mask __m, char_type __c) const
      { return this->do_is(__m, __c); }

      const char_type*
      is(const char_type *__lo, const char_type *__hi, mask *__vec) const   
      { return this->do_is(__lo, __hi, __vec); }

      const char_type*
      scan_is(mask __m, const char_type* __lo, const char_type* __hi) const
      { return this->do_scan_is(__m, __lo, __hi); }

      const char_type*
      scan_not(mask __m, const char_type* __lo, const char_type* __hi) const
      { return this->do_scan_not(__m, __lo, __hi); }

      char_type 
      toupper(char_type __c) const
      { return this->do_toupper(__c); }

      const char_type*
      toupper(char_type *__lo, const char_type* __hi) const
      { return this->do_toupper(__lo, __hi); }

      char_type 
      tolower(char_type __c) const
      { return this->do_tolower(__c); }

      const char_type*
      tolower(char_type* __lo, const char_type* __hi) const
      { return this->do_tolower(__lo, __hi); }

      char_type 
      widen(char __c) const
      { return this->do_widen(__c); }

      const char*
      widen(const char* __lo, const char* __hi, char_type* __to) const
      { return this->do_widen(__lo, __hi, __to); }

      char 
      narrow(char_type __c, char __dfault) const
      { return this->do_narrow(__c, __dfault); }

      const char_type*
      narrow(const char_type* __lo, const char_type* __hi,
	      char __dfault, char *__to) const
      { return this->do_narrow(__lo, __hi, __dfault, __to); }

    protected:
      explicit 
      __ctype_abstract_base(size_t __refs = 0): locale::facet(__refs) { }

      virtual 
      ~__ctype_abstract_base() { }
      
      virtual bool 
      do_is(mask __m, char_type __c) const = 0;

      virtual const char_type*
      do_is(const char_type* __lo, const char_type* __hi, 
	    mask* __vec) const = 0;

      virtual const char_type*
      do_scan_is(mask __m, const char_type* __lo,
		 const char_type* __hi) const = 0;

      virtual const char_type*
      do_scan_not(mask __m, const char_type* __lo, 
		  const char_type* __hi) const = 0;

      virtual char_type 
      do_toupper(char_type) const = 0;

      virtual const char_type*
      do_toupper(char_type* __lo, const char_type* __hi) const = 0;

      virtual char_type 
      do_tolower(char_type) const = 0;

      virtual const char_type*
      do_tolower(char_type* __lo, const char_type* __hi) const = 0;
      
      virtual char_type 
      do_widen(char) const = 0;

      virtual const char*
      do_widen(const char* __lo, const char* __hi, 
	       char_type* __dest) const = 0;

      virtual char 
      do_narrow(char_type, char __dfault) const = 0;

      virtual const char_type*
      do_narrow(const char_type* __lo, const char_type* __hi,
		 char __dfault, char* __dest) const = 0;
    };

  // NB: Generic, mostly useless implementation.
  template<typename _CharT>
    class ctype : public __ctype_abstract_base<_CharT>
    {
    public:
      // Types:
      typedef _CharT 		  	char_type;
      typedef typename ctype::mask 	mask;

      static locale::id 	       	id;

      explicit 
      ctype(size_t __refs = 0) : __ctype_abstract_base<_CharT>(__refs) { }

   protected:
      virtual 
      ~ctype();

      virtual bool 
      do_is(mask __m, char_type __c) const;

      virtual const char_type*
      do_is(const char_type* __lo, const char_type* __hi, mask* __vec) const;

      virtual const char_type*
      do_scan_is(mask __m, const char_type* __lo, const char_type* __hi) const;

      virtual const char_type*
      do_scan_not(mask __m, const char_type* __lo,
		  const char_type* __hi) const;

      virtual char_type 
      do_toupper(char_type __c) const;

      virtual const char_type*
      do_toupper(char_type* __lo, const char_type* __hi) const;

      virtual char_type 
      do_tolower(char_type __c) const;

      virtual const char_type*
      do_tolower(char_type* __lo, const char_type* __hi) const;

      virtual char_type 
      do_widen(char __c) const;

      virtual const char*
      do_widen(const char* __lo, const char* __hi, char_type* __dest) const;

      virtual char 
      do_narrow(char_type, char __dfault) const;

      virtual const char_type*
      do_narrow(const char_type* __lo, const char_type* __hi,
		char __dfault, char* __dest) const;
    };

  template<typename _CharT>
    locale::id ctype<_CharT>::id;

  // 22.2.1.3  ctype<char> specialization.
  template<>
    class ctype<char> : public __ctype_abstract_base<char>
    {
    public:
      // Types:
      typedef char 	       	char_type;

    protected:
      // Data Members:
      __c_locale		_M_c_locale_ctype;
      bool 		       	_M_del;
      __to_type 	       	_M_toupper;
      __to_type  	       	_M_tolower;
      const mask*              	_M_table;
      
    public:
      static locale::id        id;
      static const size_t      table_size = 1 + static_cast<unsigned char>(-1);

      explicit 
      ctype(const mask* __table = 0, bool __del = false, size_t __refs = 0);

      explicit 
      ctype(__c_locale __cloc, const mask* __table = 0, bool __del = false, 
	    size_t __refs = 0);

      inline bool 
      is(mask __m, char __c) const;
 
      inline const char*
      is(const char* __lo, const char* __hi, mask* __vec) const;
 
      inline const char*
      scan_is(mask __m, const char* __lo, const char* __hi) const;

      inline const char*
      scan_not(mask __m, const char* __lo, const char* __hi) const;
     
    protected:
      const mask* 
      table() const throw()
      { return _M_table; }

      static const mask* 
      classic_table() throw();

      virtual 
      ~ctype();

      virtual bool 
      do_is(mask __m, char_type __c) const;

      virtual const char_type*
      do_is(const char_type* __lo, const char_type* __hi, mask* __vec) const;

      virtual const char_type*
      do_scan_is(mask __m, const char_type* __lo, const char_type* __hi) const;

      virtual const char_type*
      do_scan_not(mask __m, const char_type* __lo, 
		  const char_type* __hi) const;

      virtual char_type 
      do_toupper(char_type) const;

      virtual const char_type*
      do_toupper(char_type* __lo, const char_type* __hi) const;

      virtual char_type 
      do_tolower(char_type) const;

      virtual const char_type*
      do_tolower(char_type* __lo, const char_type* __hi) const;
      
      virtual char_type 
      do_widen(char) const;

      virtual const char*
      do_widen(const char* __lo, const char* __hi, char_type* __dest) const;

      virtual char 
      do_narrow(char_type, char __dfault) const;

      virtual const char_type*
      do_narrow(const char_type* __lo, const char_type* __hi,
		char __dfault, char* __dest) const;
    };
 
  template<>
    const ctype<char>&
    use_facet<ctype<char> >(const locale& __loc);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  // 22.2.1.3  ctype<wchar_t> specialization
  template<>
    class ctype<wchar_t> : public __ctype_abstract_base<wchar_t>
    {
    public:
      // Types:
      typedef wchar_t 	       	char_type;
      typedef wctype_t	       	__wmask_type;

    protected:
      __c_locale		_M_c_locale_ctype;

    public:
      // Data Members:
      static locale::id        	id;

      explicit 
      ctype(size_t __refs = 0);

      explicit 
      ctype(__c_locale __cloc, size_t __refs = 0);

    protected:
      __wmask_type
      _M_convert_to_wmask(const mask __m) const;

      virtual 
      ~ctype();

      virtual bool 
      do_is(mask __m, char_type __c) const;

      virtual const char_type*
      do_is(const char_type* __lo, const char_type* __hi, mask* __vec) const;

      virtual const char_type*
      do_scan_is(mask __m, const char_type* __lo, const char_type* __hi) const;

      virtual const char_type*
      do_scan_not(mask __m, const char_type* __lo, 
		  const char_type* __hi) const;

      virtual char_type 
      do_toupper(char_type) const;

      virtual const char_type*
      do_toupper(char_type* __lo, const char_type* __hi) const;

      virtual char_type 
      do_tolower(char_type) const;

      virtual const char_type*
      do_tolower(char_type* __lo, const char_type* __hi) const;
      
      virtual char_type 
      do_widen(char) const;

      virtual const char*
      do_widen(const char* __lo, const char* __hi, char_type* __dest) const;

      virtual char 
      do_narrow(char_type, char __dfault) const;

      virtual const char_type*
      do_narrow(const char_type* __lo, const char_type* __hi,
		char __dfault, char* __dest) const;

    };

  template<>
    const ctype<wchar_t>&
    use_facet<ctype<wchar_t> >(const locale& __loc);
#endif //_GLIBCPP_USE_WCHAR_T

  // Include host and configuration specific ctype inlines.
  #include <bits/ctype_inline.h>

  // 22.2.1.2  Template class ctype_byname
  template<typename _CharT>
    class ctype_byname : public ctype<_CharT>
    {
    public:
      typedef _CharT 		char_type;

      explicit 
      ctype_byname(const char* __s, size_t __refs = 0);

    protected:
      virtual 
      ~ctype_byname() { };
    };

  // 22.2.1.4  Class ctype_byname specializations.
  template<>
    ctype_byname<char>::ctype_byname(const char*, size_t refs);

  template<>
    ctype_byname<wchar_t>::ctype_byname(const char*, size_t refs);

  // 22.2.1.5  Template class codecvt
  #include <bits/codecvt.h>

  // 22.2.2  The numeric category.
  class __num_base 
  {
  public:
    // NB: Code depends on the order of _S_atoms_out elements.
    // Below are the indices into _S_atoms_out.
    enum 
      {  
        _S_minus, 
        _S_plus, 
        _S_x, 
        _S_X, 
        _S_digits,
        _S_digits_end = _S_digits + 16,
        _S_udigits = _S_digits_end,  
        _S_udigits_end = _S_udigits + 16,
        _S_e = _S_digits + 14,  // For scientific notation, 'e'
        _S_E = _S_udigits + 14, // For scientific notation, 'E'
	_S_end = _S_udigits_end
      };
    
    // A list of valid numeric literals for output.  This array
    // contains chars that will be passed through the current locale's
    // ctype<_CharT>.widen() and then used to render numbers.
    // For the standard "C" locale, this is
    // "-+xX0123456789abcdef0123456789ABCDEF".
    static const char* _S_atoms_out;

  protected:
    // String literal of acceptable (narrow) input, for num_get.
    // "0123456789eEabcdfABCDF"
    static const char* _S_atoms_in;

    enum 
    {  
      _M_zero,
      _M_e = _M_zero + 10,
      _M_E = _M_zero + 11,
      _M_size = 21 + 1
    };

    // num_put
    // Construct and return valid scanf format for floating point types.
    static void
    _S_format_float(const ios_base& __io, char* __fptr, char __mod, 
		    streamsize __prec);
    
    // Construct and return valid scanf format for integer types.
    static void
    _S_format_int(const ios_base& __io, char* __fptr, char __mod, char __modl);
  };


  template<typename _CharT>
    class __locale_cache;

  template<typename _CharT>
    class numpunct : public locale::facet
    {
    public:
      // Types:
      typedef _CharT          		char_type;
      typedef basic_string<_CharT> 	string_type;

      friend class __locale_cache<numpunct<_CharT> >;

      static locale::id 		id;

    private:
      char_type 			_M_decimal_point;
      char_type 			_M_thousands_sep;
      const char* 			_M_grouping;
      const char_type* 			_M_truename;
      const char_type*			_M_falsename;

    public:
      explicit 
      numpunct(size_t __refs = 0) : locale::facet(__refs) 
      { _M_initialize_numpunct(); }

      explicit 
      numpunct(__c_locale __cloc, size_t __refs = 0) : locale::facet(__refs) 
      { _M_initialize_numpunct(__cloc); }

      char_type    
      decimal_point() const
      { return this->do_decimal_point(); }

      char_type    
      thousands_sep() const
      { return this->do_thousands_sep(); }

      string       
      grouping() const
      { return this->do_grouping(); }

      string_type  
      truename() const
      { return this->do_truename(); }

      string_type  
      falsename() const
      { return this->do_falsename(); }

    protected:
      virtual 
      ~numpunct();

      virtual char_type    
      do_decimal_point() const
      { return _M_decimal_point; }

      virtual char_type    
      do_thousands_sep() const
      { return _M_thousands_sep; }

      virtual string
      do_grouping() const
      { return _M_grouping; }

      virtual string_type  
      do_truename() const
      { return _M_truename; }

      virtual string_type  
      do_falsename() const
      { return _M_falsename; }

      // For use at construction time only.
      void 
      _M_initialize_numpunct(__c_locale __cloc = NULL);
    };

  template<typename _CharT>
    locale::id numpunct<_CharT>::id;

  template<> 
    numpunct<char>::~numpunct();

  template<> 
    void
    numpunct<char>::_M_initialize_numpunct(__c_locale __cloc);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<> 
    numpunct<wchar_t>::~numpunct();

  template<> 
    void
    numpunct<wchar_t>::_M_initialize_numpunct(__c_locale __cloc);
#endif

  template<typename _CharT>
    class numpunct_byname : public numpunct<_CharT>
    {
      // Data Member.
      __c_locale			_M_c_locale_numpunct;

    public:
      typedef _CharT               	char_type;
      typedef basic_string<_CharT> 	string_type;

      explicit 
      numpunct_byname(const char* __s, size_t __refs = 0)
      : numpunct<_CharT>(__refs)
      {
	_S_create_c_locale(_M_c_locale_numpunct, __s);
	_M_initialize_numpunct(_M_c_locale_numpunct);	
      }

    protected:
      virtual 
      ~numpunct_byname() 
      { _S_destroy_c_locale(_M_c_locale_numpunct); }
    };

  template<typename _CharT, typename _InIter>
    class num_get : public locale::facet, public __num_base
    {
    public:
      // Types:
      typedef _CharT   			char_type;
      typedef _InIter  			iter_type;

      static locale::id 		id;

      explicit 
      num_get(size_t __refs = 0) : locale::facet(__refs) { }

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, bool& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io, 
	  ios_base::iostate& __err, long& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, unsigned short& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, unsigned int& __v)   const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, unsigned long& __v)  const
      { return this->do_get(__in, __end, __io, __err, __v); }

#ifdef _GLIBCPP_USE_LONG_LONG
      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, long long& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, unsigned long long& __v)  const
      { return this->do_get(__in, __end, __io, __err, __v); }
#endif

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, float& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, double& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, long double& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type 
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, void*& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }      

    protected:
      virtual ~num_get() { }

      iter_type 
      _M_extract_float(iter_type, iter_type, ios_base&, ios_base::iostate&, 
		       string& __xtrc) const;

      iter_type 
      _M_extract_int(iter_type, iter_type, ios_base&, ios_base::iostate&, 
		     string& __xtrc, int& __base) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate&, bool&) const;


      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate&, long&) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	      unsigned short&) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	     unsigned int&) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	     unsigned long&) const;

#ifdef _GLIBCPP_USE_LONG_LONG 
      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	     long long&) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	     unsigned long long&) const;
#endif

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	     float&) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	     double&) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	     long double&) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err, 
	     void*&) const;
    };

  template<typename _CharT, typename _InIter>
    locale::id num_get<_CharT, _InIter>::id;

#if 0
  // Partial specialization for istreambuf_iterator, so can use traits_type.
  template<typename _CharT>
    class num_get<_CharT, istreambuf_iterator<_CharT> >;

      iter_type 
      _M_extract_float(iter_type, iter_type, ios_base&, ios_base::iostate&, 
		       string& __xtrc) const;

      iter_type 
      _M_extract_int(iter_type, iter_type, ios_base&, ios_base::iostate&, 
		     string& __xtrc, int& __base) const;

      virtual iter_type 
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate&, bool&) const;
#endif

  template<typename _CharT, typename _OutIter>
    class num_put : public locale::facet, public __num_base
    {
    public:
      // Types:
      typedef _CharT       	char_type;
      typedef _OutIter     	iter_type;
      static locale::id		id;

      explicit 
      num_put(size_t __refs = 0) : locale::facet(__refs) { }

      iter_type 
      put(iter_type __s, ios_base& __f, char_type __fill, bool __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      iter_type 
      put(iter_type __s, ios_base& __f, char_type __fill, long __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      iter_type 
      put(iter_type __s, ios_base& __f, char_type __fill, 
	  unsigned long __v) const
      { return this->do_put(__s, __f, __fill, __v); }

#ifdef _GLIBCPP_USE_LONG_LONG 
      iter_type 
      put(iter_type __s, ios_base& __f, char_type __fill, long long __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      iter_type 
      put(iter_type __s, ios_base& __f, char_type __fill, 
	  unsigned long long __v) const
      { return this->do_put(__s, __f, __fill, __v); }
#endif

      iter_type 
      put(iter_type __s, ios_base& __f, char_type __fill, double __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      iter_type 
      put(iter_type __s, ios_base& __f, char_type __fill, 
	  long double __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      iter_type 
      put(iter_type __s, ios_base& __f, char_type __fill, 
	  const void* __v) const
      { return this->do_put(__s, __f, __fill, __v); }

    protected:
      template<typename _ValueT>
        iter_type
        _M_convert_float(iter_type, ios_base& __io, char_type __fill, 
			 char __mod, _ValueT __v) const;

      void
      _M_group_float(const string& __grouping, char_type __sep, 
		     const char_type* __p, char_type* __new, char_type* __cs,
		     int& __len) const;

      template<typename _ValueT>
        iter_type
        _M_convert_int(iter_type, ios_base& __io, char_type __fill, 
		       _ValueT __v) const;

      void
      _M_group_int(const string& __grouping, char_type __sep, 
		   ios_base& __io, char_type* __new, char_type* __cs, 
		   int& __len) const;

      void
      _M_pad(char_type __fill, streamsize __w, ios_base& __io, 
	     char_type* __new, const char_type* __cs, int& __len) const;

#if 1
      // XXX GLIBCXX_ABI Deprecated, compatibility only.
      template<typename _ValueT>
        iter_type
        _M_convert_int(iter_type, ios_base& __io, char_type __fill, 
		       char __mod, char __modl, _ValueT __v) const;

      iter_type
      _M_widen_float(iter_type, ios_base& __io, char_type __fill, char* __cs, 
		     int __len) const;

      iter_type
      _M_widen_int(iter_type, ios_base& __io, char_type __fill, char* __cs, 
		   int __len) const;

      iter_type
      _M_insert(iter_type, ios_base& __io, char_type __fill, 
		const char_type* __ws, int __len) const;
#endif

     virtual 
      ~num_put() { };

      virtual iter_type 
      do_put(iter_type, ios_base&, char_type __fill, bool __v) const;

      virtual iter_type 
      do_put(iter_type, ios_base&, char_type __fill, long __v) const;

      virtual iter_type 
      do_put(iter_type, ios_base&, char_type __fill, unsigned long) const;

#ifdef _GLIBCPP_USE_LONG_LONG 
      virtual iter_type 
      do_put(iter_type, ios_base&, char_type __fill, long long __v) const;

      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, unsigned long long) const;
#endif

      virtual iter_type 
      do_put(iter_type, ios_base&, char_type __fill, double __v) const;

      virtual iter_type 
      do_put(iter_type, ios_base&, char_type __fill, long double __v) const;

      virtual iter_type 
      do_put(iter_type, ios_base&, char_type __fill, const void* __v) const;
    };

  template <typename _CharT, typename _OutIter>
    locale::id num_put<_CharT, _OutIter>::id;


  template<typename _CharT>
    class collate : public locale::facet
    {
    public:
      // Types:
      typedef _CharT               	char_type;
      typedef basic_string<_CharT> 	string_type;

    protected:
      // Underlying "C" library locale information saved from
      // initialization, needed by collate_byname as well.
      __c_locale			_M_c_locale_collate;
 
    public:
      static locale::id 		id;

      explicit 
      collate(size_t __refs = 0)
      : locale::facet(__refs)
      { _M_c_locale_collate = _S_c_locale; }

      explicit 
      collate(__c_locale __cloc, size_t __refs = 0) 
      : locale::facet(__refs)
      { _M_c_locale_collate = _S_clone_c_locale(__cloc); }

      int 
      compare(const _CharT* __lo1, const _CharT* __hi1,
	      const _CharT* __lo2, const _CharT* __hi2) const
      { return this->do_compare(__lo1, __hi1, __lo2, __hi2); }

      string_type 
      transform(const _CharT* __lo, const _CharT* __hi) const
      { return this->do_transform(__lo, __hi); }

      long 
      hash(const _CharT* __lo, const _CharT* __hi) const
      { return this->do_hash(__lo, __hi); }
      
      // Used to abstract out _CharT bits in virtual member functions, below.
      int
      _M_compare(const _CharT*, const _CharT*) const;

      size_t
      _M_transform(_CharT*, const _CharT*, size_t) const;

  protected:
      virtual
      ~collate() 
      { _S_destroy_c_locale(_M_c_locale_collate); }

      virtual int  
      do_compare(const _CharT* __lo1, const _CharT* __hi1,
		 const _CharT* __lo2, const _CharT* __hi2) const;

      virtual string_type 
      do_transform(const _CharT* __lo, const _CharT* __hi) const;

      virtual long   
      do_hash(const _CharT* __lo, const _CharT* __hi) const;
    };

  template<typename _CharT>
    locale::id collate<_CharT>::id;

  // Specializations.
  template<>
    int 
    collate<char>::_M_compare(const char*, const char*) const;

  template<>
    size_t
    collate<char>::_M_transform(char*, const char*, size_t) const;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<>
    int 
    collate<wchar_t>::_M_compare(const wchar_t*, const wchar_t*) const;

  template<>
    size_t
    collate<wchar_t>::_M_transform(wchar_t*, const wchar_t*, size_t) const;
#endif

  template<typename _CharT>
    class collate_byname : public collate<_CharT>
    {
    public:
      typedef _CharT               char_type;
      typedef basic_string<_CharT> string_type;

      explicit 
      collate_byname(const char* __s, size_t __refs = 0)
      : collate<_CharT>(__refs) 
      { 
	_S_destroy_c_locale(_M_c_locale_collate);
	_S_create_c_locale(_M_c_locale_collate, __s); 
      }

    protected:
      virtual   
      ~collate_byname() { }
    };


  class time_base
  {
  public:
    enum dateorder { no_order, dmy, mdy, ymd, ydm };
  };

  template<typename _CharT>
    class __timepunct : public locale::facet
    {
    public:
      // Types:
      typedef _CharT          		__char_type;
      typedef basic_string<_CharT> 	__string_type;

      static locale::id 		id;

      // List of all known timezones, with GMT first.
      static const _CharT* 		_S_timezones[14];

    protected:
      __c_locale			_M_c_locale_timepunct;
      char*				_M_name_timepunct;
      const _CharT* 			_M_date_format;
      const _CharT* 			_M_date_era_format;
      const _CharT* 			_M_time_format;
      const _CharT* 			_M_time_era_format;
      const _CharT*			_M_date_time_format;
      const _CharT*			_M_date_time_era_format;
      const _CharT* 			_M_am;
      const _CharT* 			_M_pm;
      const _CharT*			_M_am_pm_format;

      // Day names, starting with "C"'s Sunday.
      const _CharT*  			_M_day1;
      const _CharT*  			_M_day2;
      const _CharT*  			_M_day3;
      const _CharT*  			_M_day4;
      const _CharT*  			_M_day5;
      const _CharT*  			_M_day6;
      const _CharT*  			_M_day7;

      // Abbreviated day names, starting with "C"'s Sun.
      const _CharT*  			_M_day_a1;
      const _CharT*  			_M_day_a2;
      const _CharT*  			_M_day_a3;
      const _CharT*  			_M_day_a4;
      const _CharT*  			_M_day_a5;
      const _CharT*  			_M_day_a6;
      const _CharT*  			_M_day_a7;

      // Month names, starting with "C"'s January.
      const _CharT*  			_M_month01;
      const _CharT*  			_M_month02;
      const _CharT*  			_M_month03;
      const _CharT*  			_M_month04;
      const _CharT*  			_M_month05;
      const _CharT*  			_M_month06;
      const _CharT*  			_M_month07;
      const _CharT*  			_M_month08;
      const _CharT*  			_M_month09;
      const _CharT*  			_M_month10;
      const _CharT*  			_M_month11;
      const _CharT*  			_M_month12;

      // Abbreviated month names, starting with "C"'s Jan.
      const _CharT*  			_M_month_a01;
      const _CharT*  			_M_month_a02;
      const _CharT*  			_M_month_a03;
      const _CharT*  			_M_month_a04;
      const _CharT*  			_M_month_a05;
      const _CharT*  			_M_month_a06;
      const _CharT*  			_M_month_a07;
      const _CharT*  			_M_month_a08;
      const _CharT*  			_M_month_a09;
      const _CharT*  			_M_month_a10;
      const _CharT*  			_M_month_a11;
      const _CharT*  			_M_month_a12;

    public:
      explicit 
      __timepunct(size_t __refs = 0);

      explicit 
      __timepunct(__c_locale __cloc, const char* __s, size_t __refs = 0);

      void
      _M_put(_CharT* __s, size_t __maxlen, const _CharT* __format, 
	     const tm* __tm) const;

      void
      _M_date_formats(const _CharT** __date) const
      {
	// Always have default first.
	__date[0] = _M_date_format;
	__date[1] = _M_date_era_format;	
      }

      void
      _M_time_formats(const _CharT** __time) const
      {
	// Always have default first.
	__time[0] = _M_time_format;
	__time[1] = _M_time_era_format;	
      }

      void
      _M_ampm(const _CharT** __ampm) const
      { 
	__ampm[0] = _M_am;
	__ampm[1] = _M_pm;
      }      

      void
      _M_date_time_formats(const _CharT** __dt) const
      {
	// Always have default first.
	__dt[0] = _M_date_time_format;
	__dt[1] = _M_date_time_era_format;	
      }

      void
      _M_days(const _CharT** __days) const
      { 
	__days[0] = _M_day1;
	__days[1] = _M_day2;
	__days[2] = _M_day3;
	__days[3] = _M_day4;
	__days[4] = _M_day5;
	__days[5] = _M_day6;
	__days[6] = _M_day7;
      }

      void
      _M_days_abbreviated(const _CharT** __days) const
      { 
	__days[0] = _M_day_a1;
	__days[1] = _M_day_a2;
	__days[2] = _M_day_a3;
	__days[3] = _M_day_a4;
	__days[4] = _M_day_a5;
	__days[5] = _M_day_a6;
	__days[6] = _M_day_a7;
      }

      void
      _M_months(const _CharT** __months) const
      { 
	__months[0] = _M_month01;
	__months[1] = _M_month02;
	__months[2] = _M_month03;
	__months[3] = _M_month04;
	__months[4] = _M_month05;
	__months[5] = _M_month06;
	__months[6] = _M_month07;
	__months[7] = _M_month08;
	__months[8] = _M_month09;
	__months[9] = _M_month10;
	__months[10] = _M_month11;
	__months[11] = _M_month12;
      }

      void
      _M_months_abbreviated(const _CharT** __months) const
      { 
	__months[0] = _M_month_a01;
	__months[1] = _M_month_a02;
	__months[2] = _M_month_a03;
	__months[3] = _M_month_a04;
	__months[4] = _M_month_a05;
	__months[5] = _M_month_a06;
	__months[6] = _M_month_a07;
	__months[7] = _M_month_a08;
	__months[8] = _M_month_a09;
	__months[9] = _M_month_a10;
	__months[10] = _M_month_a11;
	__months[11] = _M_month_a12;
      }

    protected:
      virtual 
      ~__timepunct();

      // For use at construction time only.
      void 
      _M_initialize_timepunct(__c_locale __cloc = NULL);
    };

  template<typename _CharT>
    locale::id __timepunct<_CharT>::id;

  // Specializations.
  template<> 
    const char*
    __timepunct<char>::_S_timezones[14];

  template<> 
    void
    __timepunct<char>::_M_initialize_timepunct(__c_locale __cloc);

  template<>
    void
    __timepunct<char>::_M_put(char*, size_t, const char*, const tm*) const;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<> 
    const wchar_t*
    __timepunct<wchar_t>::_S_timezones[14];

  template<> 
    void
    __timepunct<wchar_t>::_M_initialize_timepunct(__c_locale __cloc);

  template<>
    void
    __timepunct<wchar_t>::_M_put(wchar_t*, size_t, const wchar_t*, 
				 const tm*) const;
#endif

  // Generic.
  template<typename _CharT>
    const _CharT* __timepunct<_CharT>::_S_timezones[14];

  // Include host and configuration specific timepunct functions.
  #include <bits/time_members.h>

  template<typename _CharT, typename _InIter>
    class time_get : public locale::facet, public time_base
    {
    public:
      // Types:
      typedef _CharT     		char_type;
      typedef _InIter    		iter_type;
      typedef basic_string<_CharT> 	__string_type;

      static locale::id 		id;

      explicit 
      time_get(size_t __refs = 0) 
      : locale::facet (__refs) { }

      dateorder 
      date_order()  const
      { return this->do_date_order(); }

      iter_type 
      get_time(iter_type __beg, iter_type __end, ios_base& __io, 
	       ios_base::iostate& __err, tm* __tm)  const
      { return this->do_get_time(__beg, __end, __io, __err, __tm); }

      iter_type 
      get_date(iter_type __beg, iter_type __end, ios_base& __io,
	       ios_base::iostate& __err, tm* __tm)  const
      { return this->do_get_date(__beg, __end, __io, __err, __tm); }

      iter_type 
      get_weekday(iter_type __beg, iter_type __end, ios_base& __io,
		  ios_base::iostate& __err, tm* __tm) const
      { return this->do_get_weekday(__beg, __end, __io, __err, __tm); }

      iter_type 
      get_monthname(iter_type __beg, iter_type __end, ios_base& __io, 
		    ios_base::iostate& __err, tm* __tm) const
      { return this->do_get_monthname(__beg, __end, __io, __err, __tm); }

      iter_type 
      get_year(iter_type __beg, iter_type __end, ios_base& __io,
	       ios_base::iostate& __err, tm* __tm) const
      { return this->do_get_year(__beg, __end, __io, __err, __tm); }

    protected:
      virtual 
      ~time_get() { }

      virtual dateorder 
      do_date_order() const;

      virtual iter_type 
      do_get_time(iter_type __beg, iter_type __end, ios_base& __io,
		  ios_base::iostate& __err, tm* __tm) const;

      virtual iter_type 
      do_get_date(iter_type __beg, iter_type __end, ios_base& __io,
		  ios_base::iostate& __err, tm* __tm) const;

      virtual iter_type 
      do_get_weekday(iter_type __beg, iter_type __end, ios_base&,
		     ios_base::iostate& __err, tm* __tm) const;

      virtual iter_type 
      do_get_monthname(iter_type __beg, iter_type __end, ios_base&, 
		       ios_base::iostate& __err, tm* __tm) const;

      virtual iter_type 
      do_get_year(iter_type __beg, iter_type __end, ios_base& __io,
		  ios_base::iostate& __err, tm* __tm) const;

      // Extract numeric component of length __len.
      void
      _M_extract_num(iter_type& __beg, iter_type& __end, int& __member,
		     int __min, int __max, size_t __len,
		     const ctype<_CharT>& __ctype, 
		     ios_base::iostate& __err) const;
      
      // Extract day or month name, or any unique array of string
      // literals in a const _CharT* array.
      void
      _M_extract_name(iter_type& __beg, iter_type& __end, int& __member,
		      const _CharT** __names, size_t __indexlen, 
		      ios_base::iostate& __err) const;

      // Extract on a component-by-component basis, via __format argument.
      void
      _M_extract_via_format(iter_type& __beg, iter_type& __end, ios_base& __io,
			    ios_base::iostate& __err, tm* __tm, 
			    const _CharT* __format) const;
    };

  template<typename _CharT, typename _InIter>
    locale::id time_get<_CharT, _InIter>::id;

  template<typename _CharT, typename _InIter>
    class time_get_byname : public time_get<_CharT, _InIter>
    {
    public:
      // Types:
      typedef _CharT     		char_type;
      typedef _InIter    		iter_type;

      explicit 
      time_get_byname(const char*, size_t __refs = 0) 
      : time_get<_CharT, _InIter>(__refs) { }

    protected:
      virtual 
      ~time_get_byname() { }
    };

  template<typename _CharT, typename _OutIter>
    class time_put : public locale::facet, public time_base
    {
    public:
      // Types:
      typedef _CharT     		char_type;
      typedef _OutIter   		iter_type;

      static locale::id 	     	id;

      explicit 
      time_put(size_t __refs = 0) 
      : locale::facet(__refs) { }

      iter_type 
      put(iter_type __s, ios_base& __io, char_type __fill, const tm* __tm, 
	  const _CharT* __beg, const _CharT* __end) const;

      iter_type 
      put(iter_type __s, ios_base& __io, char_type __fill,
	  const tm* __tm, char __format, char __mod = 0) const
      { return this->do_put(__s, __io, __fill, __tm, __format, __mod); }

    protected:
      virtual 
      ~time_put()
      { }

      virtual iter_type 
      do_put(iter_type __s, ios_base& __io, char_type __fill, const tm* __tm, 
	     char __format, char __mod) const;
    };

  template<typename _CharT, typename _OutIter>
    locale::id time_put<_CharT, _OutIter>::id;

  template<typename _CharT, typename _OutIter>
    class time_put_byname : public time_put<_CharT, _OutIter>
    {
    public:
      // Types:
      typedef _CharT     		char_type;
      typedef _OutIter   		iter_type;

      explicit 
      time_put_byname(const char* /*__s*/, size_t __refs = 0) 
      : time_put<_CharT, _OutIter>(__refs) 
      { };

    protected:
      virtual 
      ~time_put_byname() { }
    };


  class money_base
  {
  public:
    enum part { none, space, symbol, sign, value };
    struct pattern { char field[4]; };

    static const pattern _S_default_pattern;

    // Construct and return valid pattern consisting of some combination of:
    // space none symbol sign value
    static pattern 
    _S_construct_pattern(char __precedes, char __space, char __posn);
  };

  template<typename _CharT, bool _Intl>
    class moneypunct : public locale::facet, public money_base
    {
    public:
      // Types:
      typedef _CharT 			char_type;
      typedef basic_string<_CharT> 	string_type;

      static const bool 		intl = _Intl;
      static locale::id 		id;

    private:
      const char* 			_M_grouping;
      char_type 			_M_decimal_point;
      char_type 			_M_thousands_sep;
      const char_type* 			_M_curr_symbol;
      const char_type*			_M_positive_sign;
      const char_type*			_M_negative_sign;
      int 				_M_frac_digits;
      pattern 				_M_pos_format;
      pattern 				_M_neg_format;

    public:
      explicit 
      moneypunct(size_t __refs = 0) : locale::facet(__refs)
      { _M_initialize_moneypunct(); }

      explicit 
      moneypunct(__c_locale __cloc, const char* __s, size_t __refs = 0) 
      : locale::facet(__refs)
      { _M_initialize_moneypunct(__cloc, __s); }

      char_type
      decimal_point() const
      { return this->do_decimal_point(); }
      
      char_type
      thousands_sep() const
      { return this->do_thousands_sep(); }
      
      string 
      grouping() const
      { return this->do_grouping(); }

      string_type  
      curr_symbol() const
      { return this->do_curr_symbol(); }

      string_type  
      positive_sign() const
      { return this->do_positive_sign(); }

      string_type  
      negative_sign() const
      { return this->do_negative_sign(); }

      int          
      frac_digits() const
      { return this->do_frac_digits(); }

      pattern      
      pos_format() const
      { return this->do_pos_format(); }

      pattern      
      neg_format() const
      { return this->do_neg_format(); }

    protected:
      virtual 
      ~moneypunct();

      virtual char_type
      do_decimal_point() const
      { return _M_decimal_point; }
      
      virtual char_type
      do_thousands_sep() const
      { return _M_thousands_sep; }
      
      virtual string 
      do_grouping() const
      { return _M_grouping; }

      virtual string_type  
      do_curr_symbol()   const
      { return _M_curr_symbol; }

      virtual string_type  
      do_positive_sign() const
      { return _M_positive_sign; }

      virtual string_type  
      do_negative_sign() const
      { return _M_negative_sign; }

      virtual int          
      do_frac_digits() const
      { return _M_frac_digits; }

      virtual pattern      
      do_pos_format() const
      { return _M_pos_format; }

      virtual pattern      
      do_neg_format() const
      { return _M_neg_format; }

      // For use at construction time only.
       void 
       _M_initialize_moneypunct(__c_locale __cloc = NULL, 
				const char* __name = NULL);
    };

  template<typename _CharT, bool _Intl>
    locale::id moneypunct<_CharT, _Intl>::id;

  template<typename _CharT, bool _Intl>
    const bool moneypunct<_CharT, _Intl>::intl;

  template<>
    moneypunct<char, true>::~moneypunct();

  template<>
    moneypunct<char, false>::~moneypunct();

  template<> 
    void
    moneypunct<char, true>::_M_initialize_moneypunct(__c_locale, const char*);

  template<> 
    void
    moneypunct<char, false>::_M_initialize_moneypunct(__c_locale, const char*);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<>
    moneypunct<wchar_t, true>::~moneypunct();

  template<>
    moneypunct<wchar_t, false>::~moneypunct();

  template<> 
    void
    moneypunct<wchar_t, true>::_M_initialize_moneypunct(__c_locale, 
							const char*);

  template<> 
    void
    moneypunct<wchar_t, false>::_M_initialize_moneypunct(__c_locale, 
							 const char*);
#endif

  template<typename _CharT, bool _Intl>
    class moneypunct_byname : public moneypunct<_CharT, _Intl>
    {
      __c_locale			_M_c_locale_moneypunct;

    public:
      typedef _CharT 			char_type;
      typedef basic_string<_CharT> 	string_type;

      static const bool intl = _Intl;

      explicit 
      moneypunct_byname(const char* __s, size_t __refs = 0)
      : moneypunct<_CharT, _Intl>(__refs)
      {
	_S_create_c_locale(_M_c_locale_moneypunct, __s);
	_M_initialize_moneypunct(_M_c_locale_moneypunct);	
      }

    protected:
      virtual 
      ~moneypunct_byname() 
      { _S_destroy_c_locale(_M_c_locale_moneypunct); }
    };

  template<typename _CharT, bool _Intl>
    const bool moneypunct_byname<_CharT, _Intl>::intl;

  template<typename _CharT, typename _InIter>
    class money_get : public locale::facet
    {
    public:
      // Types:
      typedef _CharT        		char_type;
      typedef _InIter       		iter_type;
      typedef basic_string<_CharT> 	string_type;

      static locale::id 		id;

      explicit 
      money_get(size_t __refs = 0) : locale::facet(__refs) { }

      iter_type 
      get(iter_type __s, iter_type __end, bool __intl, ios_base& __io, 
	  ios_base::iostate& __err, long double& __units) const
      { return this->do_get(__s, __end, __intl, __io, __err, __units); }

      iter_type 
      get(iter_type __s, iter_type __end, bool __intl, ios_base& __io, 
	  ios_base::iostate& __err, string_type& __digits) const
      { return this->do_get(__s, __end, __intl, __io, __err, __digits); }

    protected:
      virtual 
      ~money_get() { }

      virtual iter_type 
      do_get(iter_type __s, iter_type __end, bool __intl, ios_base& __io, 
	     ios_base::iostate& __err, long double& __units) const;

      virtual iter_type 
      do_get(iter_type __s, iter_type __end, bool __intl, ios_base& __io, 
	     ios_base::iostate& __err, string_type& __digits) const;
    };

  template<typename _CharT, typename _InIter>
    locale::id money_get<_CharT, _InIter>::id;

  template<typename _CharT, typename _OutIter>
    class money_put : public locale::facet
    {
    public:
      typedef _CharT              	char_type;
      typedef _OutIter            	iter_type;
      typedef basic_string<_CharT>	string_type;

      static locale::id 		id;

      explicit 
      money_put(size_t __refs = 0) : locale::facet(__refs) { }

      iter_type 
      put(iter_type __s, bool __intl, ios_base& __io,
	  char_type __fill, long double __units) const
      { return this->do_put(__s, __intl, __io, __fill, __units); }

      iter_type 
      put(iter_type __s, bool __intl, ios_base& __io,
	  char_type __fill, const string_type& __digits) const
      { return this->do_put(__s, __intl, __io, __fill, __digits); }

    protected:
      virtual 
      ~money_put() { }

      virtual iter_type
      do_put(iter_type __s, bool __intl, ios_base& __io, char_type __fill,
	     long double __units) const;

      virtual iter_type
      do_put(iter_type __s, bool __intl, ios_base& __io, char_type __fill,
	     const string_type& __digits) const;
    };

  template<typename _CharT, typename _OutIter>
    locale::id money_put<_CharT, _OutIter>::id;


  struct messages_base
  {
    typedef int catalog;
  };

  template<typename _CharT>
    class messages : public locale::facet, public messages_base
    {
    public:
      // Types:
      typedef _CharT 			char_type;
      typedef basic_string<_CharT> 	string_type;

    protected:
      // Underlying "C" library locale information saved from
      // initialization, needed by messages_byname as well.
      __c_locale			_M_c_locale_messages;
      char*				_M_name_messages;

    public:
      static locale::id 		id;

      explicit 
      messages(size_t __refs = 0);

      // Non-standard.
      explicit 
      messages(__c_locale __cloc, const char* __s, size_t __refs = 0);

      catalog 
      open(const basic_string<char>& __s, const locale& __loc) const
      { return this->do_open(__s, __loc); }

      // Non-standard and unorthodox, yet effective.
      catalog 
      open(const basic_string<char>&, const locale&, const char*) const;

      string_type  
      get(catalog __c, int __set, int __msgid, const string_type& __s) const
      { return this->do_get(__c, __set, __msgid, __s); }

      void 
      close(catalog __c) const
      { return this->do_close(__c); }

    protected:
      virtual 
      ~messages();

      virtual catalog 
      do_open(const basic_string<char>&, const locale&) const;

      virtual string_type  
      do_get(catalog, int, int, const string_type& __dfault) const;

      virtual void    
      do_close(catalog) const;

      // Returns a locale and codeset-converted string, given a char* message.
      char*
      _M_convert_to_char(const string_type& __msg) const
      {
	// XXX
	return reinterpret_cast<char*>(const_cast<_CharT*>(__msg.c_str()));
      }

      // Returns a locale and codeset-converted string, given a char* message.
      string_type
      _M_convert_from_char(char* __msg) const
      {
	// Length of message string without terminating null.
	size_t __len = char_traits<char>::length(__msg) - 1;

	// "everybody can easily convert the string using
	// mbsrtowcs/wcsrtombs or with iconv()"
#if 0
	// Convert char* to _CharT in locale used to open catalog.
	// XXX need additional template parameter on messages class for this..
	// typedef typename codecvt<char, _CharT, _StateT> __codecvt_type;
	typedef typename codecvt<char, _CharT, mbstate_t> __codecvt_type;      

	__codecvt_type::state_type __state;
	// XXX may need to initialize state.
	//initialize_state(__state._M_init());
	
	char* __from_next;
	// XXX what size for this string?
	_CharT* __to = static_cast<_CharT*>(__builtin_alloca(__len + 1));
	const __codecvt_type& __cvt = use_facet<__codecvt_type>(_M_locale_conv);
	__cvt.out(__state, __msg, __msg + __len, __from_next,
		  __to, __to + __len + 1, __to_next);
	return string_type(__to);
#endif
#if 0
	typedef ctype<_CharT> __ctype_type;
	// const __ctype_type& __cvt = use_facet<__ctype_type>(_M_locale_msg);
	const __ctype_type& __cvt = use_facet<__ctype_type>(locale());
	// XXX Again, proper length of converted string an issue here.
	// For now, assume the converted length is not larger.
	_CharT* __dest = static_cast<_CharT*>(__builtin_alloca(__len + 1));
	__cvt.widen(__msg, __msg + __len, __dest);
	return basic_string<_CharT>(__dest);
#endif
	return string_type();
      }
     };

  template<typename _CharT>
    locale::id messages<_CharT>::id;

  // Specializations for required instantiations.
  template<>
    string
    messages<char>::do_get(catalog, int, int, const string&) const;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<>
    wstring
    messages<wchar_t>::do_get(catalog, int, int, const wstring&) const;
#endif

  template<typename _CharT>
    class messages_byname : public messages<_CharT>
    {
    public:
      typedef _CharT               	char_type;
      typedef basic_string<_CharT> 	string_type;

      explicit 
      messages_byname(const char* __s, size_t __refs = 0);

    protected:
      virtual 
      ~messages_byname() 
      { }
    };

  // Include host and configuration specific messages functions.
  #include <bits/messages_members.h>


  // Subclause convenience interfaces, inlines.
  // NB: These are inline because, when used in a loop, some compilers
  // can hoist the body out of the loop; then it's just as fast as the
  // C is*() function.
  template<typename _CharT>
    inline bool 
    isspace(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::space, __c); }

  template<typename _CharT>
    inline bool 
    isprint(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::print, __c); }

  template<typename _CharT>
    inline bool 
    iscntrl(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::cntrl, __c); }

  template<typename _CharT>
    inline bool 
    isupper(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::upper, __c); }

  template<typename _CharT>
    inline bool islower(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::lower, __c); }

  template<typename _CharT>
    inline bool 
    isalpha(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::alpha, __c); }

  template<typename _CharT>
    inline bool 
    isdigit(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::digit, __c); }

  template<typename _CharT>
    inline bool 
    ispunct(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::punct, __c); }

  template<typename _CharT>
    inline bool 
    isxdigit(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::xdigit, __c); }

  template<typename _CharT>
    inline bool 
    isalnum(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::alnum, __c); }

  template<typename _CharT>
    inline bool 
    isgraph(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::graph, __c); }

  template<typename _CharT>
    inline _CharT 
    toupper(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).toupper(__c); }

  template<typename _CharT>
    inline _CharT 
    tolower(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).tolower(__c); }

  /**
   * @if maint
   * __locale_cache objects hold information extracted from facets in
   * a form optimized for parsing and formatting.  They are stored in
   * a locale's facet array and accessed via __use_cache<_Facet>.
   *
   * The intent twofold: to avoid the costs of creating a locale
   * object and to avoid calling the virtual functions in a locale's
   * facet to look up data.
   * @endif
   */
  class __locale_cache_base
  {
    friend class std::locale::_Impl;
    friend class locale;

  public:
    virtual
    ~__locale_cache_base() { }

  };

  // This template doesn't really get used for anything except a
  // placeholder for specializations
  template<typename _Facet>
    class __locale_cache : public __locale_cache_base
    {
      // ctor
      __locale_cache(const locale&) {}
    };

  template<typename _CharT>
    class __locale_cache<numpunct<_CharT> > : public __locale_cache_base
    {
      // Types:
      typedef _CharT               	char_type;
      typedef char_traits<_CharT>       traits_type;
      typedef basic_string<_CharT>	string_type;
      
    public:
      // Data Members:

      // The sign used to separate decimal values: for standard US
      // locales, this would usually be: "."  Abstracted from
      // numpunct::decimal_point().
      _CharT                    _M_decimal_point;

      // The sign used to separate groups of digits into smaller
      // strings that the eye can parse with less difficulty: for
      // standard US locales, this would usually be: "," Abstracted
      // from numpunct::thousands_sep().
      _CharT                    _M_thousands_sep;
      
      // However the US's "false" and "true" are translated.  From
      // numpunct::truename() and numpunct::falsename(), respectively.
      const _CharT*		_M_truename;
      const _CharT*		_M_falsename;

      // If we are checking groupings. This should be equivalent to
      // numpunct::groupings().size() != 0
      bool                      _M_use_grouping;

      // If we are using numpunct's groupings, this is the current
      // grouping string in effect (from numpunct::grouping()).
      const char*               _M_grouping;

      // A list of valid numeric literals: for the standard "C"
      // locale, this is "-+xX0123456789abcdef0123456789ABCDEF".  This
      // array contains the chars after having been passed through the
      // current locale's ctype<_CharT>.widen().

      // Copied here from __locale_cache<ctype> to save multiple cache
      // access in num_put functions.
      _CharT                    _M_atoms_out[__num_base::_S_end];

      // ctor
      __locale_cache(const locale& __loc);
      __locale_cache(const locale& __loc, bool);

      ~__locale_cache()
      {
	delete [] _M_truename;
	delete [] _M_falsename;
	delete [] _M_grouping;
      }
    };
} // namespace std

#endif
