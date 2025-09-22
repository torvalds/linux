// ostream classes -*- C++ -*-

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
// ISO C++ 14882: 27.6.2  Output streams
//

#pragma GCC system_header

#include <locale>

namespace std 
{
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>::sentry::
    sentry(basic_ostream<_CharT, _Traits>& __os)
    : _M_os(__os)
    {
      // XXX MT
      if (__os.tie() && __os.good())
	__os.tie()->flush();

      if (__os.good())
	_M_ok = true;
      else
	{
	  _M_ok = false;
	  __os.setstate(ios_base::failbit);
	}
    }
  
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(__ostream_type& (*__pf)(__ostream_type&))
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // The inserters for manipulators are *not* formatted output functions.
      return __pf(*this);
    }
  
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(__ios_type& (*__pf)(__ios_type&))
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // The inserters for manipulators are *not* formatted output functions.
      __pf(*this);
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(ios_base& (*__pf)(ios_base&))
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // The inserters for manipulators are *not* formatted output functions.
      __pf(*this);
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(bool __n)
    {
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try 
	    {
	      _M_check_facet(this->_M_fnumput);
	      const __numput_type& __np = *this->_M_fnumput;
	      if (__np.put(*this, *this, this->fill(), __n).failed())
		__err |= ios_base::badbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(long __n)
    {
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try 
	    {
	      bool __b = false;
	      char_type __c = this->fill();
	      ios_base::fmtflags __fmt = this->flags() & ios_base::basefield;
	      _M_check_facet(this->_M_fnumput);
	      const __numput_type& __np = *this->_M_fnumput;
	      if ((__fmt & ios_base::oct) || (__fmt & ios_base::hex))
		{
		  unsigned long __l = static_cast<unsigned long>(__n);
		  __b = __np.put(*this, *this, __c, __l).failed();
		}
	      else
		__b = __np.put(*this, *this, __c, __n).failed();
	      if (__b)  
		__err |= ios_base::badbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(unsigned long __n)
    {
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try 
	    {
	      _M_check_facet(this->_M_fnumput);
	      const __numput_type& __np = *this->_M_fnumput;
	      if (__np.put(*this, *this, this->fill(), __n).failed())
		__err |= ios_base::badbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

#ifdef _GLIBCPP_USE_LONG_LONG
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(long long __n)
    {
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try 
	    {
	      bool __b = false;
	      char_type __c = this->fill();
	      ios_base::fmtflags __fmt = this->flags() & ios_base::basefield;
	      _M_check_facet(this->_M_fnumput);
	      const __numput_type& __np = *this->_M_fnumput;
	      if ((__fmt & ios_base::oct) || (__fmt & ios_base::hex))
		{
		  unsigned long long __l;
		  __l = static_cast<unsigned long long>(__n);
		  __b = __np.put(*this, *this, __c, __l).failed();
		}
	      else
		__b = __np.put(*this, *this, __c, __n).failed();
	      if (__b)  
		__err |= ios_base::badbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(unsigned long long __n)
    {
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try 
	    {
	      _M_check_facet(this->_M_fnumput);
	      const __numput_type& __np = *this->_M_fnumput;
	      if (__np.put(*this, *this, this->fill(), __n).failed())
		__err |= ios_base::badbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }
#endif
  
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(double __n)
    {
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try 
	    {
	      _M_check_facet(this->_M_fnumput);
	      const __numput_type& __np = *this->_M_fnumput;
	      if (__np.put(*this, *this, this->fill(), __n).failed())
		__err |= ios_base::badbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }
  
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(long double __n)
    {
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try 
	    {
	      _M_check_facet(this->_M_fnumput);
	      const __numput_type& __np = *this->_M_fnumput;
	      if (__np.put(*this, *this, this->fill(), __n).failed())
		__err |= ios_base::badbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(const void* __n)
    {
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try 
	    {
	      _M_check_facet(this->_M_fnumput);
	      const __numput_type& __np = *this->_M_fnumput;
	      if (__np.put(*this, *this, this->fill(), __n).failed())
		__err |= ios_base::badbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>& 
    basic_ostream<_CharT, _Traits>::
    operator<<(__streambuf_type* __sbin)
    {
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      sentry __cerb(*this);
      if (__cerb && __sbin)
	{
	  try
	    {
	      if (!__copy_streambufs(*this, __sbin, this->rdbuf()))
		__err |= ios_base::failbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::failbit); }
	}
      else if (!__sbin)
	__err |= ios_base::badbit;
      if (__err)
	this->setstate(__err);
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::put(char_type __c)
    { 
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // basic_ostream::put(char_type) is an unformatted output function.
      // DR 63. Exception-handling policy for unformatted output.
      // Unformatted output functions should catch exceptions thrown
      // from streambuf members.
      sentry __cerb(*this);
      if (__cerb) 
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try
	    {
	      int_type __put = this->rdbuf()->sputc(__c); 
	      if (traits_type::eq_int_type(__put, traits_type::eof()))
		__err |= ios_base::badbit;
	    }
	  catch (...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;     
    }   
      
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::write(const _CharT* __s, streamsize __n)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // basic_ostream::write(const char_type*, streamsize) is an
      // unformatted output function.
      // DR 63. Exception-handling policy for unformatted output.
      // Unformatted output functions should catch exceptions thrown
      // from streambuf members.
      sentry __cerb(*this);
      if (__cerb)
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try
	    {
	      streamsize __put = this->rdbuf()->sputn(__s, __n);
	      if (__put != __n)
		__err |= ios_base::badbit;
	    }
 	  catch (...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::flush()
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // DR 60. What is a formatted input function?
      // basic_ostream::flush() is *not* an unformatted output function.
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      try
	{
	  if (this->rdbuf() && this->rdbuf()->pubsync() == -1)
	    __err |= ios_base::badbit;
	}
      catch(...)
	{ this->_M_setstate(ios_base::badbit); }
      if (__err)
	this->setstate(__err);
      return *this;
    }
  
  template<typename _CharT, typename _Traits>
    typename basic_ostream<_CharT, _Traits>::pos_type
    basic_ostream<_CharT, _Traits>::tellp()
    {
      pos_type __ret = pos_type(-1);
      try
	{
	  if (!this->fail())
	    __ret = this->rdbuf()->pubseekoff(0, ios_base::cur, ios_base::out);
	}
      catch(...)
	{ this->_M_setstate(ios_base::badbit); }
      return __ret;
    }


  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::seekp(pos_type __pos)
    {
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      try
	{
	  if (!this->fail())
	    {
	      // _GLIBCXX_RESOLVE_LIB_DEFECTS
	      // 136.  seekp, seekg setting wrong streams?
	      pos_type __p = this->rdbuf()->pubseekpos(__pos, ios_base::out);
	      
	      // 129. Need error indication from seekp() and seekg()
	      if (__p == pos_type(off_type(-1)))
		__err |= ios_base::failbit;
	    }
	}
      catch(...)
	{ this->_M_setstate(ios_base::badbit); }
      if (__err)
	this->setstate(__err);
      return *this;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    basic_ostream<_CharT, _Traits>::
    seekp(off_type __off, ios_base::seekdir __dir)
    {
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      try
	{
	  if (!this->fail())
	    {
	      // _GLIBCXX_RESOLVE_LIB_DEFECTS
	      // 136.  seekp, seekg setting wrong streams?
	      pos_type __p = this->rdbuf()->pubseekoff(__off, __dir, 
						       ios_base::out);
	      
	      // 129. Need error indication from seekp() and seekg()
	      if (__p == pos_type(off_type(-1)))
		__err |= ios_base::failbit;
	    }
	}
      catch(...)
	{ this->_M_setstate(ios_base::badbit); }
      if (__err)
	this->setstate(__err);
      return *this;
    }

  // 27.6.2.5.4 Character inserters.
  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __out, _CharT __c)
    {
      typedef basic_ostream<_CharT, _Traits> __ostream_type;
      typename __ostream_type::sentry __cerb(__out);
      if (__cerb)
	{
	  try 
	    {
	      const streamsize __w = __out.width() > 0 ? __out.width() : 0;
	      _CharT* __pads = static_cast<_CharT*>(__builtin_alloca(sizeof(_CharT) * (__w + 1)));
	      __pads[0] = __c;
	      streamsize __len = 1;
	      if (__w > __len)
		{
		  __pad<_CharT, _Traits>::_S_pad(__out, __out.fill(), __pads, 
						 &__c, __w, __len, false);
		  __len = __w;
		}
	      __out.write(__pads, __len);
	      __out.width(0);
	    }
	  catch(...)
	    { __out._M_setstate(ios_base::badbit); }
	}
      return __out;
    }
  
  // Specializations.
  template <class _Traits> 
    basic_ostream<char, _Traits>&
    operator<<(basic_ostream<char, _Traits>& __out, char __c)
    {
      typedef basic_ostream<char, _Traits> __ostream_type;
      typename __ostream_type::sentry __cerb(__out);
      if (__cerb)
	{
	  try 
	    {
	      const streamsize __w = __out.width() > 0 ? __out.width() : 0;
	      char* __pads = static_cast<char*>(__builtin_alloca(__w + 1));
	      __pads[0] = __c;
	      streamsize __len = 1;
	      if (__w > __len)
		{
		  __pad<char, _Traits>::_S_pad(__out, __out.fill(), __pads, 
					       &__c, __w, __len, false);
		  __len = __w;
		}
	      __out.write(__pads, __len);
	      __out.width(0);
	    }
	  catch(...)
	    { __out._M_setstate(ios_base::badbit); }
	}
      return __out;
     }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __out, const _CharT* __s)
    {
      typedef basic_ostream<_CharT, _Traits> __ostream_type;
      typename __ostream_type::sentry __cerb(__out);
      if (__cerb && __s)
	{
	  try 
	    {
	      const streamsize __w = __out.width() > 0 ? __out.width() : 0;
	      _CharT* __pads = static_cast<_CharT*>(__builtin_alloca(sizeof(_CharT) * __w));
	      streamsize __len = static_cast<streamsize>(_Traits::length(__s));
	      if (__w > __len)
		{
		  __pad<_CharT, _Traits>::_S_pad(__out, __out.fill(), __pads, 
						 __s, __w, __len, false);
		  __s = __pads;
		  __len = __w;
		}
	      __out.write(__s, __len);
	      __out.width(0);
	    }
	  catch(...)
	    { __out._M_setstate(ios_base::badbit); }
	}
      else if (!__s)
	__out.setstate(ios_base::badbit);
      return __out;
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __out, const char* __s)
    {
      typedef basic_ostream<_CharT, _Traits> __ostream_type;
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
// 167.  Improper use of traits_type::length()
// Note that this is only in 'Review' status.
      typedef char_traits<char>		     __traits_type;
#endif
      typename __ostream_type::sentry __cerb(__out);
      if (__cerb && __s)
	{
	  size_t __clen = __traits_type::length(__s);
	  _CharT* __ws = static_cast<_CharT*>(__builtin_alloca(sizeof(_CharT) * (__clen + 1)));
	  for (size_t  __i = 0; __i < __clen; ++__i)
	    __ws[__i] = __out.widen(__s[__i]);
	  _CharT* __str = __ws;
	  
	  try 
	    {
	      streamsize __len = static_cast<streamsize>(__clen);
	      const streamsize __w = __out.width() > 0 ? __out.width() : 0;
	      _CharT* __pads = static_cast<_CharT*>(__builtin_alloca(sizeof(_CharT) * __w));
	      
	      if (__w > __len)
		{
		  __pad<_CharT, _Traits>::_S_pad(__out, __out.fill(), __pads, 
						 __ws, __w, __len, false);
		  __str = __pads;
		  __len = __w;
		}
	      __out.write(__str, __len);
	      __out.width(0);
	    }
	  catch(...)
	    { __out._M_setstate(ios_base::badbit); }
	}
      else if (!__s)
	__out.setstate(ios_base::badbit);
      return __out;
    }

  // Partial specializations.
  template<class _Traits>
    basic_ostream<char, _Traits>&
    operator<<(basic_ostream<char, _Traits>& __out, const char* __s)
    {
      typedef basic_ostream<char, _Traits> __ostream_type;
      typename __ostream_type::sentry __cerb(__out);
      if (__cerb && __s)
	{
	  try 
	    {
	      const streamsize __w = __out.width() > 0 ? __out.width() : 0;
	      char* __pads = static_cast<char*>(__builtin_alloca(__w));
	      streamsize __len = static_cast<streamsize>(_Traits::length(__s));

	      if (__w > __len)
		{
		  __pad<char, _Traits>::_S_pad(__out, __out.fill(), __pads, 
						 __s, __w, __len, false);
		  __s = __pads;
		  __len = __w;
		}
	      __out.write(__s, __len);
	      __out.width(0);
	    }
	  catch(...)
	    { __out._M_setstate(ios_base::badbit); }
	}
      else if (!__s)
	__out.setstate(ios_base::badbit);
      return __out;
    }

  // 21.3.7.9 basic_string::operator<<
  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __out,
	       const basic_string<_CharT, _Traits, _Alloc>& __str)
    { 
      typedef basic_ostream<_CharT, _Traits> __ostream_type;
      typename __ostream_type::sentry __cerb(__out);
      if (__cerb)
	{
	  const _CharT* __s = __str.data();
	  const streamsize __w = __out.width() > 0 ? __out.width() : 0;
	  _CharT* __pads = static_cast<_CharT*>(__builtin_alloca(sizeof(_CharT) * __w));
	  streamsize __len = static_cast<streamsize>(__str.size());
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
	  // 25. String operator<< uses width() value wrong
#endif
	  if (__w > __len)
	    {
	      __pad<_CharT, _Traits>::_S_pad(__out, __out.fill(), __pads, __s, 
					     __w, __len, false);
	      __s = __pads;
	      __len = __w;
	    }
	  streamsize __res = __out.rdbuf()->sputn(__s, __len);
	  __out.width(0);
	  if (__res != __len)
	    __out.setstate(ios_base::failbit);
	}
      return __out;
    }

  // Inhibit implicit instantiations for required instantiations,
  // which are defined via explicit instantiations elsewhere.  
  // NB:  This syntax is a GNU extension.
#if _GLIBCPP_EXTERN_TEMPLATE
  extern template class basic_ostream<char>;
  extern template ostream& endl(ostream&);
  extern template ostream& ends(ostream&);
  extern template ostream& flush(ostream&);
  extern template ostream& operator<<(ostream&, char);
  extern template ostream& operator<<(ostream&, unsigned char);
  extern template ostream& operator<<(ostream&, signed char);
  extern template ostream& operator<<(ostream&, const char*);
  extern template ostream& operator<<(ostream&, const unsigned char*);
  extern template ostream& operator<<(ostream&, const signed char*);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  extern template class basic_ostream<wchar_t>;
  extern template wostream& endl(wostream&);
  extern template wostream& ends(wostream&);
  extern template wostream& flush(wostream&);
  extern template wostream& operator<<(wostream&, wchar_t);
  extern template wostream& operator<<(wostream&, char);
  extern template wostream& operator<<(wostream&, const wchar_t*);
  extern template wostream& operator<<(wostream&, const char*);
#endif
#endif
} // namespace std
