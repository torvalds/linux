// Locale support (codecvt) -*- C++ -*-

// Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006
//  Free Software Foundation, Inc.
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

//
// ISO C++ 14882: 22.2.1.5 Template class codecvt
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

/** @file ext/codecvt_specializations.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _EXT_CODECVT_SPECIALIZATIONS_H
#define _EXT_CODECVT_SPECIALIZATIONS_H 1

#include <bits/c++config.h>

#ifdef _GLIBCXX_USE_ICONV

#include <locale>
#include <iconv.h>

  // XXX
  // Define this here so codecvt.cc can have _S_max_size definition.
#define _GLIBCXX_USE_ENCODING_STATE 1

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  /// @brief  Extension to use icov for dealing with character encodings.
  // This includes conversions and comparisons between various character
  // sets.  This object encapsulates data that may need to be shared between
  // char_traits, codecvt and ctype.
  class encoding_state
  {
  public:
    // Types: 
    // NB: A conversion descriptor subsumes and enhances the
    // functionality of a simple state type such as mbstate_t.
    typedef iconv_t	descriptor_type;
    
  protected:
    // Name of internal character set encoding.
    std::string	       	_M_int_enc;

    // Name of external character set encoding.
    std::string  	_M_ext_enc;

    // Conversion descriptor between external encoding to internal encoding.
    descriptor_type	_M_in_desc;

    // Conversion descriptor between internal encoding to external encoding.
    descriptor_type	_M_out_desc;

    // The byte-order marker for the external encoding, if necessary.
    int			_M_ext_bom;

    // The byte-order marker for the internal encoding, if necessary.
    int			_M_int_bom;

    // Number of external bytes needed to construct one complete
    // character in the internal encoding.
    // NB: -1 indicates variable, or stateful, encodings.
    int 		_M_bytes;

  public:
    explicit 
    encoding_state() 
    : _M_in_desc(0), _M_out_desc(0), _M_ext_bom(0), _M_int_bom(0), _M_bytes(0)
    { }

    explicit 
    encoding_state(const char* __int, const char* __ext, 
		   int __ibom = 0, int __ebom = 0, int __bytes = 1)
    : _M_int_enc(__int), _M_ext_enc(__ext), _M_in_desc(0), _M_out_desc(0), 
      _M_ext_bom(__ebom), _M_int_bom(__ibom), _M_bytes(__bytes)
    { init(); }

    // 21.1.2 traits typedefs
    // p4
    // typedef STATE_T state_type
    // requires: state_type shall meet the requirements of
    // CopyConstructible types (20.1.3)
    // NB: This does not preseve the actual state of the conversion
    // descriptor member, but it does duplicate the encoding
    // information.
    encoding_state(const encoding_state& __obj) : _M_in_desc(0), _M_out_desc(0)
    { construct(__obj); }

    // Need assignment operator as well.
    encoding_state&
    operator=(const encoding_state& __obj)
    {
      construct(__obj);
      return *this;
    }

    ~encoding_state()
    { destroy(); } 

    bool
    good() const throw()
    { 
      const descriptor_type __err = reinterpret_cast<iconv_t>(-1);
      bool __test = _M_in_desc && _M_in_desc != __err; 
      __test &=  _M_out_desc && _M_out_desc != __err;
      return __test;
    }
    
    int
    character_ratio() const
    { return _M_bytes; }

    const std::string
    internal_encoding() const
    { return _M_int_enc; }

    int 
    internal_bom() const
    { return _M_int_bom; }

    const std::string
    external_encoding() const
    { return _M_ext_enc; }

    int 
    external_bom() const
    { return _M_ext_bom; }

    const descriptor_type&
    in_descriptor() const
    { return _M_in_desc; }

    const descriptor_type&
    out_descriptor() const
    { return _M_out_desc; }

  protected:
    void
    init()
    {
      const descriptor_type __err = reinterpret_cast<iconv_t>(-1);
      const bool __have_encodings = _M_int_enc.size() && _M_ext_enc.size();
      if (!_M_in_desc && __have_encodings)
	{
	  _M_in_desc = iconv_open(_M_int_enc.c_str(), _M_ext_enc.c_str());
	  if (_M_in_desc == __err)
	    std::__throw_runtime_error(__N("encoding_state::_M_init "
				    "creating iconv input descriptor failed"));
	}
      if (!_M_out_desc && __have_encodings)
	{
	  _M_out_desc = iconv_open(_M_ext_enc.c_str(), _M_int_enc.c_str());
	  if (_M_out_desc == __err)
	    std::__throw_runtime_error(__N("encoding_state::_M_init "
				  "creating iconv output descriptor failed"));
	}
    }

    void
    construct(const encoding_state& __obj)
    {
      destroy();
      _M_int_enc = __obj._M_int_enc;
      _M_ext_enc = __obj._M_ext_enc;
      _M_ext_bom = __obj._M_ext_bom;
      _M_int_bom = __obj._M_int_bom;
      _M_bytes = __obj._M_bytes;
      init();
    }

    void
    destroy() throw()
    {
      const descriptor_type __err = reinterpret_cast<iconv_t>(-1);
      if (_M_in_desc && _M_in_desc != __err) 
	{
	  iconv_close(_M_in_desc);
	  _M_in_desc = 0;
	}
      if (_M_out_desc && _M_out_desc != __err) 
	{
	  iconv_close(_M_out_desc);
	  _M_out_desc = 0;
	}
    }
  };

  /// @brief  encoding_char_traits.
  // Custom traits type with encoding_state for the state type, and the
  // associated fpos<encoding_state> for the position type, all other
  // bits equivalent to the required char_traits instantiations.
  template<typename _CharT>
    struct encoding_char_traits : public std::char_traits<_CharT>
    {
      typedef encoding_state				state_type;
      typedef typename std::fpos<state_type>		pos_type;
    };

_GLIBCXX_END_NAMESPACE


_GLIBCXX_BEGIN_NAMESPACE(std)

  using __gnu_cxx::encoding_state;

  /// @brief  codecvt<InternT, _ExternT, encoding_state> specialization.
  // This partial specialization takes advantage of iconv to provide
  // code conversions between a large number of character encodings.
  template<typename _InternT, typename _ExternT>
    class codecvt<_InternT, _ExternT, encoding_state>
    : public __codecvt_abstract_base<_InternT, _ExternT, encoding_state>
    {
    public:      
      // Types:
      typedef codecvt_base::result			result;
      typedef _InternT 					intern_type;
      typedef _ExternT 					extern_type;
      typedef __gnu_cxx::encoding_state 		state_type;
      typedef state_type::descriptor_type 		descriptor_type;

      // Data Members:
      static locale::id 		id;

      explicit 
      codecvt(size_t __refs = 0)
      : __codecvt_abstract_base<intern_type, extern_type, state_type>(__refs)
      { }

      explicit 
      codecvt(state_type& __enc, size_t __refs = 0)
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
      do_length(state_type&, const extern_type* __from, 
		const extern_type* __end, size_t __max) const;

      virtual int 
      do_max_length() const throw();
    };

  template<typename _InternT, typename _ExternT>
    locale::id 
    codecvt<_InternT, _ExternT, encoding_state>::id;

  // This adaptor works around the signature problems of the second
  // argument to iconv():  SUSv2 and others use 'const char**', but glibc 2.2
  // uses 'char**', which matches the POSIX 1003.1-2001 standard.
  // Using this adaptor, g++ will do the work for us.
  template<typename _Tp>
    inline size_t
    __iconv_adaptor(size_t(*__func)(iconv_t, _Tp, size_t*, char**, size_t*),
                    iconv_t __cd, char** __inbuf, size_t* __inbytes,
                    char** __outbuf, size_t* __outbytes)
    { return __func(__cd, (_Tp)__inbuf, __inbytes, __outbuf, __outbytes); }

  template<typename _InternT, typename _ExternT>
    codecvt_base::result
    codecvt<_InternT, _ExternT, encoding_state>::
    do_out(state_type& __state, const intern_type* __from, 
	   const intern_type* __from_end, const intern_type*& __from_next,
	   extern_type* __to, extern_type* __to_end,
	   extern_type*& __to_next) const
    {
      result __ret = codecvt_base::error;
      if (__state.good())
	{
	  const descriptor_type& __desc = __state.out_descriptor();
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
	  int __int_bom = __state.internal_bom();
	  if (__int_bom)
	    {	  
	      size_t __size = __from_end - __from;
	      intern_type* __cfixed = static_cast<intern_type*>
		(__builtin_alloca(sizeof(intern_type) * (__size + 1)));
	      __cfixed[0] = static_cast<intern_type>(__int_bom);
	      char_traits<intern_type>::copy(__cfixed + 1, __from, __size);
	      __cfrom = reinterpret_cast<char*>(__cfixed);
	      __conv = __iconv_adaptor(iconv, __desc, &__cfrom,
                                        &__fbytes, &__cto, &__tbytes); 
	    }
	  else
	    {
	      intern_type* __cfixed = const_cast<intern_type*>(__from);
	      __cfrom = reinterpret_cast<char*>(__cfixed);
	      __conv = __iconv_adaptor(iconv, __desc, &__cfrom, &__fbytes, 
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
    codecvt<_InternT, _ExternT, encoding_state>::
    do_unshift(state_type& __state, extern_type* __to, 
	       extern_type* __to_end, extern_type*& __to_next) const
    {
      result __ret = codecvt_base::error;
      if (__state.good())
	{
	  const descriptor_type& __desc = __state.in_descriptor();
	  const size_t __tmultiple = sizeof(intern_type);
	  size_t __tlen = __tmultiple * (__to_end - __to); 
	  
	  // Argument list for iconv specifies a byte sequence. Thus,
	  // all to/from arrays must be brutally casted to char*.
	  char* __cto = reinterpret_cast<char*>(__to);
	  size_t __conv = __iconv_adaptor(iconv,__desc, NULL, NULL,
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
    codecvt<_InternT, _ExternT, encoding_state>::
    do_in(state_type& __state, const extern_type* __from, 
	  const extern_type* __from_end, const extern_type*& __from_next,
	  intern_type* __to, intern_type* __to_end, 
	  intern_type*& __to_next) const
    { 
      result __ret = codecvt_base::error;
      if (__state.good())
	{
	  const descriptor_type& __desc = __state.in_descriptor();
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
	  int __ext_bom = __state.external_bom();
	  if (__ext_bom)
	    {	  
	      size_t __size = __from_end - __from;
	      extern_type* __cfixed =  static_cast<extern_type*>
		(__builtin_alloca(sizeof(extern_type) * (__size + 1)));
	      __cfixed[0] = static_cast<extern_type>(__ext_bom);
	      char_traits<extern_type>::copy(__cfixed + 1, __from, __size);
	      __cfrom = reinterpret_cast<char*>(__cfixed);
	      __conv = __iconv_adaptor(iconv, __desc, &__cfrom,
                                       &__flen, &__cto, &__tlen); 
	    }
	  else
	    {
	      extern_type* __cfixed = const_cast<extern_type*>(__from);
	      __cfrom = reinterpret_cast<char*>(__cfixed);
	      __conv = __iconv_adaptor(iconv, __desc, &__cfrom,
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
    codecvt<_InternT, _ExternT, encoding_state>::
    do_encoding() const throw()
    {
      int __ret = 0;
      if (sizeof(_ExternT) <= sizeof(_InternT))
	__ret = sizeof(_InternT) / sizeof(_ExternT);
      return __ret; 
    }
  
  template<typename _InternT, typename _ExternT>
    bool 
    codecvt<_InternT, _ExternT, encoding_state>::
    do_always_noconv() const throw()
    { return false; }
  
  template<typename _InternT, typename _ExternT>
    int 
    codecvt<_InternT, _ExternT, encoding_state>::
    do_length(state_type&, const extern_type* __from, 
	      const extern_type* __end, size_t __max) const
    { return std::min(__max, static_cast<size_t>(__end - __from)); }

  // _GLIBCXX_RESOLVE_LIB_DEFECTS
  // 74.  Garbled text for codecvt::do_max_length
  template<typename _InternT, typename _ExternT>
    int 
    codecvt<_InternT, _ExternT, encoding_state>::
    do_max_length() const throw()
    { return 1; }

_GLIBCXX_END_NAMESPACE

#endif

#endif
