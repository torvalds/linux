// Input streams -*- C++ -*-

// Copyright (C) 2004, 2005 Free Software Foundation, Inc.
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
// ISO C++ 14882: 27.6.1  Input streams
//

#include <istream>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<>
    basic_istream<char>&
    basic_istream<char>::
    getline(char_type* __s, streamsize __n, char_type __delim)
    {
      _M_gcount = 0;
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      sentry __cerb(*this, true);
      if (__cerb)
	{
          try
	    {
	      const int_type __idelim = traits_type::to_int_type(__delim);
	      const int_type __eof = traits_type::eof();
	      __streambuf_type* __sb = this->rdbuf();
	      int_type __c = __sb->sgetc();
	      
	      while (_M_gcount + 1 < __n
		     && !traits_type::eq_int_type(__c, __eof)
		     && !traits_type::eq_int_type(__c, __idelim))
		{
		  streamsize __size = std::min(streamsize(__sb->egptr()
							  - __sb->gptr()),
					       streamsize(__n - _M_gcount
							  - 1));
		  if (__size > 1)
		    {
		      const char_type* __p = traits_type::find(__sb->gptr(),
							       __size,
							       __delim);
		      if (__p)
			__size = __p - __sb->gptr();
		      traits_type::copy(__s, __sb->gptr(), __size);
		      __s += __size;
		      __sb->gbump(__size);
		      _M_gcount += __size;
		      __c = __sb->sgetc();
		    }
		  else
		    {
		      *__s++ = traits_type::to_char_type(__c);
		      ++_M_gcount;
		      __c = __sb->snextc();
		    }
		}

	      if (traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	      else if (traits_type::eq_int_type(__c, __idelim))
		{
		  ++_M_gcount;		  
		  __sb->sbumpc();
		}
	      else
		__err |= ios_base::failbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	}
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 243. get and getline when sentry reports failure.
      if (__n > 0)
	*__s = char_type();
      if (!_M_gcount)
	__err |= ios_base::failbit;
      if (__err)
	this->setstate(__err);
      return *this;
    }

  template<>
    basic_istream<char>&
    basic_istream<char>::
    ignore(streamsize __n, int_type __delim)
    {
      if (traits_type::eq_int_type(__delim, traits_type::eof()))
	return ignore(__n);

      _M_gcount = 0;
      sentry __cerb(*this, true);
      if (__cerb && __n > 0)
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try
	    {
	      const char_type __cdelim = traits_type::to_char_type(__delim);	      
	      const int_type __eof = traits_type::eof();
	      __streambuf_type* __sb = this->rdbuf();
	      int_type __c = __sb->sgetc();

	      bool __large_ignore = false;
	      while (true)
		{
		  while (_M_gcount < __n
			 && !traits_type::eq_int_type(__c, __eof)
			 && !traits_type::eq_int_type(__c, __delim))
		    {
		      streamsize __size = std::min(streamsize(__sb->egptr()
							      - __sb->gptr()),
						   streamsize(__n - _M_gcount));
		      if (__size > 1)
			{
			  const char_type* __p = traits_type::find(__sb->gptr(),
								   __size,
								   __cdelim);
			  if (__p)
			    __size = __p - __sb->gptr();
			  __sb->gbump(__size);
			  _M_gcount += __size;
			  __c = __sb->sgetc();
			}
		      else
			{
			  ++_M_gcount;
			  __c = __sb->snextc();
			}
		    }
		  if (__n == numeric_limits<streamsize>::max()
		      && !traits_type::eq_int_type(__c, __eof)
		      && !traits_type::eq_int_type(__c, __delim))
		    {
		      _M_gcount = numeric_limits<streamsize>::min();
		      __large_ignore = true;
		    }
		  else
		    break;
		}

	      if (__large_ignore)
		_M_gcount = numeric_limits<streamsize>::max();

	      if (traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	      else if (traits_type::eq_int_type(__c, __delim))
		{
		  if (_M_gcount < numeric_limits<streamsize>::max())
		    ++_M_gcount;
		  __sb->sbumpc();
		}
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<>
    basic_istream<char>&
    operator>>(basic_istream<char>& __in, char* __s)
    {
      typedef basic_istream<char>       	__istream_type;
      typedef __istream_type::int_type		__int_type;
      typedef __istream_type::char_type		__char_type;
      typedef __istream_type::traits_type	__traits_type;
      typedef __istream_type::__streambuf_type  __streambuf_type;
      typedef __istream_type::__ctype_type	__ctype_type;

      streamsize __extracted = 0;
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      __istream_type::sentry __cerb(__in, false);
      if (__cerb)
	{
	  try
	    {
	      // Figure out how many characters to extract.
	      streamsize __num = __in.width();
	      if (__num <= 0)
		__num = numeric_limits<streamsize>::max();

	      const __ctype_type& __ct = use_facet<__ctype_type>(__in.getloc());

	      const __int_type __eof = __traits_type::eof();
	      __streambuf_type* __sb = __in.rdbuf();
	      __int_type __c = __sb->sgetc();

	      while (__extracted < __num - 1
		     && !__traits_type::eq_int_type(__c, __eof)
		     && !__ct.is(ctype_base::space,
				 __traits_type::to_char_type(__c)))
		{
		  streamsize __size = std::min(streamsize(__sb->egptr()
							  - __sb->gptr()),
					       streamsize(__num - __extracted
							  - 1));
		  if (__size > 1)
		    {
		      __size = (__ct.scan_is(ctype_base::space,
					     __sb->gptr() + 1,
					     __sb->gptr() + __size)
				- __sb->gptr());
		      __traits_type::copy(__s, __sb->gptr(), __size);
		      __s += __size;
		      __sb->gbump(__size);
		      __extracted += __size;
		      __c = __sb->sgetc();
		    }
		  else
		    {
		      *__s++ = __traits_type::to_char_type(__c);
		      ++__extracted;
		      __c = __sb->snextc();
		    }
		}

	      if (__traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;

	      // _GLIBCXX_RESOLVE_LIB_DEFECTS
	      // 68.  Extractors for char* should store null at end
	      *__s = __char_type();
	      __in.width(0);
	    }
	  catch(...)
	    { __in._M_setstate(ios_base::badbit); }
	}
      if (!__extracted)
	__err |= ios_base::failbit;
      if (__err)
	__in.setstate(__err);
      return __in;
    }

  template<>
    basic_istream<char>&
    operator>>(basic_istream<char>& __in, basic_string<char>& __str)
    {
      typedef basic_istream<char>       	__istream_type;
      typedef __istream_type::int_type		__int_type;
      typedef __istream_type::char_type		__char_type;
      typedef __istream_type::traits_type	__traits_type;
      typedef __istream_type::__streambuf_type  __streambuf_type;
      typedef __istream_type::__ctype_type	__ctype_type;
      typedef basic_string<char>        	__string_type;
      typedef __string_type::size_type		__size_type;

      __size_type __extracted = 0;
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      __istream_type::sentry __cerb(__in, false);
      if (__cerb)
	{
	  try
	    {
	      __str.erase();
	      const streamsize __w = __in.width();
	      const __size_type __n = __w > 0 ? static_cast<__size_type>(__w)
		                              : __str.max_size();
	      const __ctype_type& __ct = use_facet<__ctype_type>(__in.getloc());
	      const __int_type __eof = __traits_type::eof();
	      __streambuf_type* __sb = __in.rdbuf();
	      __int_type __c = __sb->sgetc();

	      while (__extracted < __n
		     && !__traits_type::eq_int_type(__c, __eof)
		     && !__ct.is(ctype_base::space,
				 __traits_type::to_char_type(__c)))
		{
		  streamsize __size = std::min(streamsize(__sb->egptr()
							  - __sb->gptr()),
					       streamsize(__n - __extracted));
		  if (__size > 1)
		    {
		      __size = (__ct.scan_is(ctype_base::space,
					     __sb->gptr() + 1,
					     __sb->gptr() + __size)
				- __sb->gptr());
		      __str.append(__sb->gptr(), __size);
		      __sb->gbump(__size);
		      __extracted += __size;
		      __c = __sb->sgetc();
		    }
		  else
		    {
		      __str += __traits_type::to_char_type(__c);
		      ++__extracted;
		      __c = __sb->snextc();
		    }		  
		}

	      if (__traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	      __in.width(0);
	    }
	  catch(...)
	    {
	      // _GLIBCXX_RESOLVE_LIB_DEFECTS
	      // 91. Description of operator>> and getline() for string<>
	      // might cause endless loop
	      __in._M_setstate(ios_base::badbit);
	    }
	}
      if (!__extracted)
	__err |= ios_base::failbit;
      if (__err)
	__in.setstate(__err);
      return __in;
    }

  template<>
    basic_istream<char>&
    getline(basic_istream<char>& __in, basic_string<char>& __str,
	    char __delim)
    {
      typedef basic_istream<char>       	__istream_type;
      typedef __istream_type::int_type		__int_type;
      typedef __istream_type::char_type		__char_type;
      typedef __istream_type::traits_type	__traits_type;
      typedef __istream_type::__streambuf_type  __streambuf_type;
      typedef __istream_type::__ctype_type	__ctype_type;
      typedef basic_string<char>        	__string_type;
      typedef __string_type::size_type		__size_type;

      __size_type __extracted = 0;
      const __size_type __n = __str.max_size();
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      __istream_type::sentry __cerb(__in, true);
      if (__cerb)
	{
	  try
	    {
	      __str.erase();
	      const __int_type __idelim = __traits_type::to_int_type(__delim);
	      const __int_type __eof = __traits_type::eof();
	      __streambuf_type* __sb = __in.rdbuf();
	      __int_type __c = __sb->sgetc();

	      while (__extracted < __n
		     && !__traits_type::eq_int_type(__c, __eof)
		     && !__traits_type::eq_int_type(__c, __idelim))
		{
		  streamsize __size = std::min(streamsize(__sb->egptr()
							  - __sb->gptr()),
					       streamsize(__n - __extracted));
		  if (__size > 1)
		    {
		      const __char_type* __p = __traits_type::find(__sb->gptr(),
								   __size,
								   __delim);
		      if (__p)
			__size = __p - __sb->gptr();
		      __str.append(__sb->gptr(), __size);
		      __sb->gbump(__size);
		      __extracted += __size;
		      __c = __sb->sgetc();
		    }
		  else
		    {
		      __str += __traits_type::to_char_type(__c);
		      ++__extracted;
		      __c = __sb->snextc();
		    }		  
		}

	      if (__traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	      else if (__traits_type::eq_int_type(__c, __idelim))
		{
		  ++__extracted;
		  __sb->sbumpc();
		}
	      else
		__err |= ios_base::failbit;
	    }
	  catch(...)
	    {
	      // _GLIBCXX_RESOLVE_LIB_DEFECTS
	      // 91. Description of operator>> and getline() for string<>
	      // might cause endless loop
	      __in._M_setstate(ios_base::badbit);
	    }
	}
      if (!__extracted)
	__err |= ios_base::failbit;
      if (__err)
	__in.setstate(__err);
      return __in;
    }

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    basic_istream<wchar_t>&
    basic_istream<wchar_t>::
    getline(char_type* __s, streamsize __n, char_type __delim)
    {
      _M_gcount = 0;
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      sentry __cerb(*this, true);
      if (__cerb)
	{
          try
	    {
	      const int_type __idelim = traits_type::to_int_type(__delim);
	      const int_type __eof = traits_type::eof();
	      __streambuf_type* __sb = this->rdbuf();
	      int_type __c = __sb->sgetc();
	      
	      while (_M_gcount + 1 < __n
		     && !traits_type::eq_int_type(__c, __eof)
		     && !traits_type::eq_int_type(__c, __idelim))
		{
		  streamsize __size = std::min(streamsize(__sb->egptr()
							  - __sb->gptr()),
					       streamsize(__n - _M_gcount
							  - 1));
		  if (__size > 1)
		    {
		      const char_type* __p = traits_type::find(__sb->gptr(),
							       __size,
							       __delim);
		      if (__p)
			__size = __p - __sb->gptr();
		      traits_type::copy(__s, __sb->gptr(), __size);
		      __s += __size;
		      __sb->gbump(__size);
		      _M_gcount += __size;
		      __c = __sb->sgetc();
		    }
		  else
		    {
		      *__s++ = traits_type::to_char_type(__c);
		      ++_M_gcount;
		      __c = __sb->snextc();
		    }
		}

	      if (traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	      else if (traits_type::eq_int_type(__c, __idelim))
		{
		  ++_M_gcount;		  
		  __sb->sbumpc();
		}
	      else
		__err |= ios_base::failbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	}
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 243. get and getline when sentry reports failure.
      if (__n > 0)
	*__s = char_type();
      if (!_M_gcount)
	__err |= ios_base::failbit;
      if (__err)
	this->setstate(__err);
      return *this;
    }

  template<>
    basic_istream<wchar_t>&
    basic_istream<wchar_t>::
    ignore(streamsize __n, int_type __delim)
    {
      if (traits_type::eq_int_type(__delim, traits_type::eof()))
	return ignore(__n);

      _M_gcount = 0;
      sentry __cerb(*this, true);
      if (__cerb && __n > 0)
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try
	    {
	      const char_type __cdelim = traits_type::to_char_type(__delim);	      
	      const int_type __eof = traits_type::eof();
	      __streambuf_type* __sb = this->rdbuf();
	      int_type __c = __sb->sgetc();

	      bool __large_ignore = false;
	      while (true)
		{
		  while (_M_gcount < __n
			 && !traits_type::eq_int_type(__c, __eof)
			 && !traits_type::eq_int_type(__c, __delim))
		    {
		      streamsize __size = std::min(streamsize(__sb->egptr()
							      - __sb->gptr()),
						   streamsize(__n - _M_gcount));
		      if (__size > 1)
			{
			  const char_type* __p = traits_type::find(__sb->gptr(),
								   __size,
								   __cdelim);
			  if (__p)
			    __size = __p - __sb->gptr();
			  __sb->gbump(__size);
			  _M_gcount += __size;
			  __c = __sb->sgetc();
			}
		      else
			{
			  ++_M_gcount;
			  __c = __sb->snextc();
			}
		    }
		  if (__n == numeric_limits<streamsize>::max()
		      && !traits_type::eq_int_type(__c, __eof)
		      && !traits_type::eq_int_type(__c, __delim))
		    {
		      _M_gcount = numeric_limits<streamsize>::min();
		      __large_ignore = true;
		    }
		  else
		    break;
		}

	      if (__large_ignore)
		_M_gcount = numeric_limits<streamsize>::max();

	      if (traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	      else if (traits_type::eq_int_type(__c, __delim))
		{
		  if (_M_gcount < numeric_limits<streamsize>::max())
		    ++_M_gcount;
		  __sb->sbumpc();
		}
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }

  template<>
    basic_istream<wchar_t>&
    getline(basic_istream<wchar_t>& __in, basic_string<wchar_t>& __str,
	    wchar_t __delim)
    {
      typedef basic_istream<wchar_t>       	__istream_type;
      typedef __istream_type::int_type		__int_type;
      typedef __istream_type::char_type		__char_type;
      typedef __istream_type::traits_type	__traits_type;
      typedef __istream_type::__streambuf_type  __streambuf_type;
      typedef __istream_type::__ctype_type	__ctype_type;
      typedef basic_string<wchar_t>        	__string_type;
      typedef __string_type::size_type		__size_type;

      __size_type __extracted = 0;
      const __size_type __n = __str.max_size();
      ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
      __istream_type::sentry __cerb(__in, true);
      if (__cerb)
	{
	  try
	    {
	      __str.erase();
	      const __int_type __idelim = __traits_type::to_int_type(__delim);
	      const __int_type __eof = __traits_type::eof();
	      __streambuf_type* __sb = __in.rdbuf();
	      __int_type __c = __sb->sgetc();

	      while (__extracted < __n
		     && !__traits_type::eq_int_type(__c, __eof)
		     && !__traits_type::eq_int_type(__c, __idelim))
		{
		  streamsize __size = std::min(streamsize(__sb->egptr()
							  - __sb->gptr()),
					       streamsize(__n - __extracted));
		  if (__size > 1)
		    {
		      const __char_type* __p = __traits_type::find(__sb->gptr(),
								   __size,
								   __delim);
		      if (__p)
			__size = __p - __sb->gptr();
		      __str.append(__sb->gptr(), __size);
		      __sb->gbump(__size);
		      __extracted += __size;
		      __c = __sb->sgetc();
		    }
		  else
		    {
		      __str += __traits_type::to_char_type(__c);
		      ++__extracted;
		      __c = __sb->snextc();
		    }		  
		}

	      if (__traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	      else if (__traits_type::eq_int_type(__c, __idelim))
		{
		  ++__extracted;
		  __sb->sbumpc();
		}
	      else
		__err |= ios_base::failbit;
	    }
	  catch(...)
	    {
	      // _GLIBCXX_RESOLVE_LIB_DEFECTS
	      // 91. Description of operator>> and getline() for string<>
	      // might cause endless loop
	      __in._M_setstate(ios_base::badbit);
	    }
	}
      if (!__extracted)
	__err |= ios_base::failbit;
      if (__err)
	__in.setstate(__err);
      return __in;
    }
#endif

_GLIBCXX_END_NAMESPACE
