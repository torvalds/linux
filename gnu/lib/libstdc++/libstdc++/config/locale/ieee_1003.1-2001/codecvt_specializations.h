// Locale support (codecvt) -*- C++ -*-

// Copyright (C) 2000, 2001, 2002 Free Software Foundation, Inc.
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

// Warning: this file is not meant for user inclusion.  Use <locale>.

// Written by Benjamin Kosnik <bkoz@cygnus.com>

  // XXX
  // Define this here to codecvt.cc can have _S_max_size definition.
#define _GLIBCPP_USE___ENC_TRAITS 1

  // Extension to use icov for dealing with character encodings,
  // including conversions and comparisons between various character
  // sets.  This object encapsulates data that may need to be shared between
  // char_traits, codecvt and ctype.
  class __enc_traits
  {
  public:
    // Types: 
    // NB: A conversion descriptor subsumes and enhances the
    // functionality of a simple state type such as mbstate_t.
    typedef iconv_t	__desc_type;
    
  protected:
    // Data Members:
    // Max size of charset encoding name
    static const int 	_S_max_size = 32;
    // Name of internal character set encoding.
    char	       	_M_int_enc[_S_max_size];
    // Name of external character set encoding.
    char  	       	_M_ext_enc[_S_max_size];

    // Conversion descriptor between external encoding to internal encoding.
    __desc_type		_M_in_desc;
    // Conversion descriptor between internal encoding to external encoding.
    __desc_type		_M_out_desc;

    // Details the byte-order marker for the external encoding, if necessary.
    int			_M_ext_bom;

    // Details the byte-order marker for the internal encoding, if necessary.
    int			_M_int_bom;

  public:
    explicit __enc_traits() 
    : _M_in_desc(0), _M_out_desc(0), _M_ext_bom(0), _M_int_bom(0) 
    {
      memset(_M_int_enc, 0, _S_max_size);
      memset(_M_ext_enc, 0, _S_max_size);
    }

    explicit __enc_traits(const char* __int, const char* __ext, 
			  int __ibom = 0, int __ebom = 0)
    : _M_in_desc(0), _M_out_desc(0), _M_ext_bom(0), _M_int_bom(0)
    {
      strncpy(_M_int_enc, __int, _S_max_size);
      strncpy(_M_ext_enc, __ext, _S_max_size);
    }

    // 21.1.2 traits typedefs
    // p4
    // typedef STATE_T state_type
    // requires: state_type shall meet the requirements of
    // CopyConstructible types (20.1.3)
    __enc_traits(const __enc_traits& __obj): _M_in_desc(0), _M_out_desc(0)
    {
      strncpy(_M_int_enc, __obj._M_int_enc, _S_max_size);
      strncpy(_M_ext_enc, __obj._M_ext_enc, _S_max_size);
      _M_ext_bom = __obj._M_ext_bom;
      _M_int_bom = __obj._M_int_bom;
    }

    // Need assignment operator as well.
    __enc_traits&
    operator=(const __enc_traits& __obj)
    {
      strncpy(_M_int_enc, __obj._M_int_enc, _S_max_size);
      strncpy(_M_ext_enc, __obj._M_ext_enc, _S_max_size);
      _M_in_desc = 0;
      _M_out_desc = 0;
      _M_ext_bom = __obj._M_ext_bom;
      _M_int_bom = __obj._M_int_bom;
      return *this;
    }

    ~__enc_traits()
    {
      __desc_type __err = reinterpret_cast<iconv_t>(-1);
      if (_M_in_desc && _M_in_desc != __err) 
	iconv_close(_M_in_desc);
      if (_M_out_desc && _M_out_desc != __err) 
	iconv_close(_M_out_desc);
    } 

    void
    _M_init()
    {
      const __desc_type __err = reinterpret_cast<iconv_t>(-1);
      if (!_M_in_desc)
	{
	  _M_in_desc = iconv_open(_M_int_enc, _M_ext_enc);
	  if (_M_in_desc == __err)
	    __throw_runtime_error("creating iconv input descriptor failed.");
	}
      if (!_M_out_desc)
	{
	  _M_out_desc = iconv_open(_M_ext_enc, _M_int_enc);
	  if (_M_out_desc == __err)
	    __throw_runtime_error("creating iconv output descriptor failed.");
	}
    }

    bool
    _M_good()
    { 
      const __desc_type __err = reinterpret_cast<iconv_t>(-1);
      bool __test = _M_in_desc && _M_in_desc != __err; 
      __test &=  _M_out_desc && _M_out_desc != __err;
      return __test;
    }

    const __desc_type* 
    _M_get_in_descriptor()
    { return &_M_in_desc; }

    const __desc_type* 
    _M_get_out_descriptor()
    { return &_M_out_desc; }

    int 
    _M_get_external_bom()
    { return _M_ext_bom; }

    int 
    _M_get_internal_bom()
    { return _M_int_bom; }

    const char* 
    _M_get_internal_enc()
    { return _M_int_enc; }

    const char* 
    _M_get_external_enc()
    { return _M_ext_enc; }
  };

  // Partial specialization
  // This specialization takes advantage of iconv to provide code
  // conversions between a large number of character encodings.
  template<typename _InternT, typename _ExternT>
    class codecvt<_InternT, _ExternT, __enc_traits>
    : public __codecvt_abstract_base<_InternT, _ExternT, __enc_traits>
    {
    public:      
      // Types:
      typedef codecvt_base::result			result;
      typedef _InternT 					intern_type;
      typedef _ExternT 					extern_type;
      typedef __enc_traits 				state_type;
      typedef __enc_traits::__desc_type 		__desc_type;
      typedef __enc_traits				__enc_type;

      // Data Members:
      static locale::id 		id;

      explicit 
      codecvt(size_t __refs = 0)
      : __codecvt_abstract_base<intern_type, extern_type, state_type>(__refs)
      { }

      explicit 
      codecvt(__enc_type* __enc, size_t __refs = 0)
      : __codecvt_abstract_base<intern_type, extern_type, state_type>(__refs)
      { }

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

  template<typename _InternT, typename _ExternT>
    locale::id 
    codecvt<_InternT, _ExternT, __enc_traits>::id;

  // This adaptor works around the signature problems of the second
  // argument to iconv():  SUSv2 and others use 'const char**', but glibc 2.2
  // uses 'char**', which matches the POSIX 1003.1-2001 standard.
  // Using this adaptor, g++ will do the work for us.
  template<typename _T>
    inline size_t
    __iconv_adaptor(size_t(*__func)(iconv_t, _T, size_t*, char**, size_t*),
                    iconv_t __cd, char** __inbuf, size_t* __inbytes,
                    char** __outbuf, size_t* __outbytes)
    { return __func(__cd, (_T)__inbuf, __inbytes, __outbuf, __outbytes); }

  template<typename _InternT, typename _ExternT>
    codecvt_base::result
    codecvt<_InternT, _ExternT, __enc_traits>::
    do_out(state_type& __state, const intern_type* __from, 
	   const intern_type* __from_end, const intern_type*& __from_next,
	   extern_type* __to, extern_type* __to_end,
	   extern_type*& __to_next) const
    {
      result __ret = codecvt_base::error;
      if (__state._M_good())
	{
	  typedef state_type::__desc_type	__desc_type;
	  const __desc_type* __desc = __state._M_get_out_descriptor();
	  const size_t __fmultiple = sizeof(intern_type);
	  size_t __fbytes = __fmultiple * (__from_end - __from);
	  const size_t __tmultiple = sizeof(extern_type);
	  size_t __tbytes = __tmultiple * (__to_end - __to); 
	  
	  // Argument list for iconv specifies a byte sequence. Thus,
	  // all to/from arrays must be brutally casted to char*.
	  char* __cto = reinterpret_cast<char*>(__to);
	  char* __cfrom;
	  size_t __conv;

	  // Some encodings need a byte order marker as the first item
	  // in the byte stream, to designate endian-ness. The default
	  // value for the byte order marker is NULL, so if this is
	  // the case, it's not necessary and we can just go on our
	  // merry way.
	  int __int_bom = __state._M_get_internal_bom();
	  if (__int_bom)
	    {	  
	      size_t __size = __from_end - __from;
	      intern_type* __cfixed = static_cast<intern_type*>(__builtin_alloca(sizeof(intern_type) * (__size + 1)));
	      __cfixed[0] = static_cast<intern_type>(__int_bom);
	      char_traits<intern_type>::copy(__cfixed + 1, __from, __size);
	      __cfrom = reinterpret_cast<char*>(__cfixed);
	      __conv = __iconv_adaptor(iconv, *__desc, &__cfrom,
                                        &__fbytes, &__cto, &__tbytes); 
	    }
	  else
	    {
	      intern_type* __cfixed = const_cast<intern_type*>(__from);
	      __cfrom = reinterpret_cast<char*>(__cfixed);
	      __conv = __iconv_adaptor(iconv, *__desc, &__cfrom, &__fbytes, 
				       &__cto, &__tbytes); 
	    }

	  if (__conv != size_t(-1))
	    {
	      __from_next = reinterpret_cast<const intern_type*>(__cfrom);
	      __to_next = reinterpret_cast<extern_type*>(__cto);
	      __ret = codecvt_base::ok;
	    }
	  else 
	    {
	      if (__fbytes < __fmultiple * (__from_end - __from))
		{
		  __from_next = reinterpret_cast<const intern_type*>(__cfrom);
		  __to_next = reinterpret_cast<extern_type*>(__cto);
		  __ret = codecvt_base::partial;
		}
	      else
		__ret = codecvt_base::error;
	    }
	}
      return __ret; 
    }

  template<typename _InternT, typename _ExternT>
    codecvt_base::result
    codecvt<_InternT, _ExternT, __enc_traits>::
    do_unshift(state_type& __state, extern_type* __to, 
	       extern_type* __to_end, extern_type*& __to_next) const
    {
      result __ret = codecvt_base::error;
      if (__state._M_good())
	{
	  typedef state_type::__desc_type	__desc_type;
	  const __desc_type* __desc = __state._M_get_in_descriptor();
	  const size_t __tmultiple = sizeof(intern_type);
	  size_t __tlen = __tmultiple * (__to_end - __to); 
	  
	  // Argument list for iconv specifies a byte sequence. Thus,
	  // all to/from arrays must be brutally casted to char*.
	  char* __cto = reinterpret_cast<char*>(__to);
	  size_t __conv = __iconv_adaptor(iconv,*__desc, NULL, NULL,
                                          &__cto, &__tlen); 
	  
	  if (__conv != size_t(-1))
	    {
	      __to_next = reinterpret_cast<extern_type*>(__cto);
	      if (__tlen == __tmultiple * (__to_end - __to))
		__ret = codecvt_base::noconv;
	      else if (__tlen == 0)
		__ret = codecvt_base::ok;
	      else
		__ret = codecvt_base::partial;
	    }
	  else 
	    __ret = codecvt_base::error;
	}
      return __ret; 
    }
   
  template<typename _InternT, typename _ExternT>
    codecvt_base::result
    codecvt<_InternT, _ExternT, __enc_traits>::
    do_in(state_type& __state, const extern_type* __from, 
	  const extern_type* __from_end, const extern_type*& __from_next,
	  intern_type* __to, intern_type* __to_end, 
	  intern_type*& __to_next) const
    { 
      result __ret = codecvt_base::error;
      if (__state._M_good())
	{
	  typedef state_type::__desc_type	__desc_type;
	  const __desc_type* __desc = __state._M_get_in_descriptor();
	  const size_t __fmultiple = sizeof(extern_type);
	  size_t __flen = __fmultiple * (__from_end - __from);
	  const size_t __tmultiple = sizeof(intern_type);
	  size_t __tlen = __tmultiple * (__to_end - __to); 
	  
	  // Argument list for iconv specifies a byte sequence. Thus,
	  // all to/from arrays must be brutally casted to char*.
	  char* __cto = reinterpret_cast<char*>(__to);
	  char* __cfrom;
	  size_t __conv;

	  // Some encodings need a byte order marker as the first item
	  // in the byte stream, to designate endian-ness. The default
	  // value for the byte order marker is NULL, so if this is
	  // the case, it's not necessary and we can just go on our
	  // merry way.
	  int __ext_bom = __state._M_get_external_bom();
	  if (__ext_bom)
	    {	  
	      size_t __size = __from_end - __from;
	      extern_type* __cfixed =  static_cast<extern_type*>(__builtin_alloca(sizeof(extern_type) * (__size + 1)));
	      __cfixed[0] = static_cast<extern_type>(__ext_bom);
	      char_traits<extern_type>::copy(__cfixed + 1, __from, __size);
	      __cfrom = reinterpret_cast<char*>(__cfixed);
	      __conv = __iconv_adaptor(iconv, *__desc, &__cfrom,
                                       &__flen, &__cto, &__tlen); 
	    }
	  else
	    {
	      extern_type* __cfixed = const_cast<extern_type*>(__from);
	      __cfrom = reinterpret_cast<char*>(__cfixed);
	      __conv = __iconv_adaptor(iconv, *__desc, &__cfrom,
                                       &__flen, &__cto, &__tlen); 
	    }

	  
	  if (__conv != size_t(-1))
	    {
	      __from_next = reinterpret_cast<const extern_type*>(__cfrom);
	      __to_next = reinterpret_cast<intern_type*>(__cto);
	      __ret = codecvt_base::ok;
	    }
	  else 
	    {
	      if (__flen < static_cast<size_t>(__from_end - __from))
		{
		  __from_next = reinterpret_cast<const extern_type*>(__cfrom);
		  __to_next = reinterpret_cast<intern_type*>(__cto);
		  __ret = codecvt_base::partial;
		}
	      else
		__ret = codecvt_base::error;
	    }
	}
      return __ret; 
    }
  
  template<typename _InternT, typename _ExternT>
    int 
    codecvt<_InternT, _ExternT, __enc_traits>::
    do_encoding() const throw()
    {
      int __ret = 0;
      if (sizeof(_ExternT) <= sizeof(_InternT))
	__ret = sizeof(_InternT)/sizeof(_ExternT);
      return __ret; 
    }
  
  template<typename _InternT, typename _ExternT>
    bool 
    codecvt<_InternT, _ExternT, __enc_traits>::
    do_always_noconv() const throw()
    { return false; }
  
  template<typename _InternT, typename _ExternT>
    int 
    codecvt<_InternT, _ExternT, __enc_traits>::
    do_length(const state_type&, const extern_type* __from, 
	      const extern_type* __end, size_t __max) const
    { return min(__max, static_cast<size_t>(__end - __from)); }

#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
// 74.  Garbled text for codecvt::do_max_length
  template<typename _InternT, typename _ExternT>
    int 
    codecvt<_InternT, _ExternT, __enc_traits>::
    do_max_length() const throw()
    { return 1; }
#endif
