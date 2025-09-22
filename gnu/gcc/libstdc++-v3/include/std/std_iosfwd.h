// Forwarding declarations -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2001, 2002, 2003, 2005
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

/** @file iosfwd
 *  This is a Standard C++ Library header.
 */

//
// ISO C++ 14882: 27.2  Forward declarations
//

#ifndef _GLIBCXX_IOSFWD
#define _GLIBCXX_IOSFWD 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <bits/c++locale.h> 
#include <bits/c++io.h> 
#include <cctype>		// For isspace, etc.
#include <bits/stringfwd.h> 	// For string forward declarations.
#include <bits/postypes.h>
#include <bits/functexcept.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_ios;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_streambuf;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_istream;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_ostream;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_iostream;

  template<typename _CharT, typename _Traits = char_traits<_CharT>,
	    typename _Alloc = allocator<_CharT> >
    class basic_stringbuf;

  template<typename _CharT, typename _Traits = char_traits<_CharT>,
	   typename _Alloc = allocator<_CharT> >
    class basic_istringstream;

  template<typename _CharT, typename _Traits = char_traits<_CharT>,
	   typename _Alloc = allocator<_CharT> >
    class basic_ostringstream;

  template<typename _CharT, typename _Traits = char_traits<_CharT>,
	   typename _Alloc = allocator<_CharT> >
    class basic_stringstream;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_filebuf;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_ifstream;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_ofstream;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class basic_fstream;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class istreambuf_iterator;

  template<typename _CharT, typename _Traits = char_traits<_CharT> >
    class ostreambuf_iterator;

  // _GLIBCXX_RESOLVE_LIB_DEFECTS
  // Not included.   (??? Apparently no LWG number?)
  class ios_base; 

  /** 
   *  @defgroup s27_2_iosfwd I/O Forward Declarations
   *
   *  Nearly all of the I/O classes are parameterized on the type of
   *  characters they read and write.  (The major exception is ios_base at
   *  the top of the hierarchy.)  This is a change from pre-Standard
   *  streams, which were not templates.
   *
   *  For ease of use and compatibility, all of the basic_* I/O-related
   *  classes are given typedef names for both of the builtin character
   *  widths (wide and narrow).  The typedefs are the same as the
   *  pre-Standard names, for example:
   *
   *  @code
   *     typedef basic_ifstream<char>  ifstream;
   *  @endcode
   *
   *  Because properly forward-declaring these classes can be difficult, you
   *  should not do it yourself.  Instead, include the &lt;iosfwd&gt;
   *  header, which contains only declarations of all the I/O classes as
   *  well as the typedefs.  Trying to forward-declare the typedefs
   *  themselves (e.g., "class ostream;") is not valid ISO C++.
   *
   *  For more specific declarations, see
   *  http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#10
   *
   *  @{
  */
  typedef basic_ios<char> 		ios;		///< @isiosfwd
  typedef basic_streambuf<char> 	streambuf;	///< @isiosfwd
  typedef basic_istream<char> 		istream;	///< @isiosfwd
  typedef basic_ostream<char> 		ostream;	///< @isiosfwd
  typedef basic_iostream<char> 		iostream;	///< @isiosfwd
  typedef basic_stringbuf<char> 	stringbuf;	///< @isiosfwd
  typedef basic_istringstream<char> 	istringstream;	///< @isiosfwd
  typedef basic_ostringstream<char> 	ostringstream;	///< @isiosfwd
  typedef basic_stringstream<char> 	stringstream;	///< @isiosfwd
  typedef basic_filebuf<char> 		filebuf;	///< @isiosfwd
  typedef basic_ifstream<char> 		ifstream;	///< @isiosfwd
  typedef basic_ofstream<char> 		ofstream;	///< @isiosfwd
  typedef basic_fstream<char> 		fstream;	///< @isiosfwd

#ifdef _GLIBCXX_USE_WCHAR_T
  typedef basic_ios<wchar_t> 		wios;		///< @isiosfwd
  typedef basic_streambuf<wchar_t> 	wstreambuf;	///< @isiosfwd
  typedef basic_istream<wchar_t> 	wistream;	///< @isiosfwd
  typedef basic_ostream<wchar_t> 	wostream;	///< @isiosfwd
  typedef basic_iostream<wchar_t> 	wiostream;	///< @isiosfwd
  typedef basic_stringbuf<wchar_t> 	wstringbuf;	///< @isiosfwd
  typedef basic_istringstream<wchar_t> 	wistringstream;	///< @isiosfwd
  typedef basic_ostringstream<wchar_t> 	wostringstream;	///< @isiosfwd
  typedef basic_stringstream<wchar_t> 	wstringstream;	///< @isiosfwd
  typedef basic_filebuf<wchar_t> 	wfilebuf;	///< @isiosfwd
  typedef basic_ifstream<wchar_t> 	wifstream;	///< @isiosfwd
  typedef basic_ofstream<wchar_t> 	wofstream;	///< @isiosfwd
  typedef basic_fstream<wchar_t> 	wfstream;	///< @isiosfwd
#endif
  /** @}  */

_GLIBCXX_END_NAMESPACE

#endif /* _GLIBCXX_IOSFWD */
