// Iostreams base classes -*- C++ -*-

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
// ISO C++ 14882: 27.4  Iostreams base classes
//

#include <ios>
#include <ostream>
#include <istream>
#include <fstream>
#include <bits/atomicity.h>
#include <ext/stdio_filebuf.h>
#ifdef _GLIBCPP_HAVE_UNISTD_H
#include <unistd.h>
#endif

namespace __gnu_cxx
{
  // Extern declarations for global objects in src/globals.cc.
  extern stdio_filebuf<char> buf_cout;
  extern stdio_filebuf<char> buf_cin;
  extern stdio_filebuf<char> buf_cerr;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  extern stdio_filebuf<wchar_t> buf_wcout;
  extern stdio_filebuf<wchar_t> buf_wcin;
  extern stdio_filebuf<wchar_t> buf_wcerr;
#endif
} // namespace __gnu_cxx

namespace std 
{
  using namespace __gnu_cxx;
  
  extern istream cin;
  extern ostream cout;
  extern ostream cerr;
  extern ostream clog;

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  extern wistream wcin;
  extern wostream wcout;
  extern wostream wcerr;
  extern wostream wclog;
#endif

  // Definitions for static const data members of __ios_flags.
  const __ios_flags::__int_type __ios_flags::_S_boolalpha;
  const __ios_flags::__int_type __ios_flags::_S_dec;
  const __ios_flags::__int_type __ios_flags::_S_fixed;
  const __ios_flags::__int_type __ios_flags::_S_hex;
  const __ios_flags::__int_type __ios_flags::_S_internal;
  const __ios_flags::__int_type __ios_flags::_S_left;
  const __ios_flags::__int_type __ios_flags::_S_oct;
  const __ios_flags::__int_type __ios_flags::_S_right;
  const __ios_flags::__int_type __ios_flags::_S_scientific;
  const __ios_flags::__int_type __ios_flags::_S_showbase;
  const __ios_flags::__int_type __ios_flags::_S_showpoint;
  const __ios_flags::__int_type __ios_flags::_S_showpos;
  const __ios_flags::__int_type __ios_flags::_S_skipws;
  const __ios_flags::__int_type __ios_flags::_S_unitbuf;
  const __ios_flags::__int_type __ios_flags::_S_uppercase;
  const __ios_flags::__int_type __ios_flags::_S_adjustfield;
  const __ios_flags::__int_type __ios_flags::_S_basefield;
  const __ios_flags::__int_type __ios_flags::_S_floatfield;

  const __ios_flags::__int_type __ios_flags::_S_badbit;
  const __ios_flags::__int_type __ios_flags::_S_eofbit;
  const __ios_flags::__int_type __ios_flags::_S_failbit;

  const __ios_flags::__int_type __ios_flags::_S_app;
  const __ios_flags::__int_type __ios_flags::_S_ate;
  const __ios_flags::__int_type __ios_flags::_S_bin;
  const __ios_flags::__int_type __ios_flags::_S_in;
  const __ios_flags::__int_type __ios_flags::_S_out;
  const __ios_flags::__int_type __ios_flags::_S_trunc;

  // Definitions for static const members of ios_base.
  const ios_base::fmtflags ios_base::boolalpha;
  const ios_base::fmtflags ios_base::dec;
  const ios_base::fmtflags ios_base::fixed;
  const ios_base::fmtflags ios_base::hex;
  const ios_base::fmtflags ios_base::internal;
  const ios_base::fmtflags ios_base::left;
  const ios_base::fmtflags ios_base::oct;
  const ios_base::fmtflags ios_base::right;
  const ios_base::fmtflags ios_base::scientific;
  const ios_base::fmtflags ios_base::showbase;
  const ios_base::fmtflags ios_base::showpoint;
  const ios_base::fmtflags ios_base::showpos;
  const ios_base::fmtflags ios_base::skipws;
  const ios_base::fmtflags ios_base::unitbuf;
  const ios_base::fmtflags ios_base::uppercase;
  const ios_base::fmtflags ios_base::adjustfield;
  const ios_base::fmtflags ios_base::basefield;
  const ios_base::fmtflags ios_base::floatfield;

  const ios_base::iostate ios_base::badbit;
  const ios_base::iostate ios_base::eofbit;
  const ios_base::iostate ios_base::failbit;
  const ios_base::iostate ios_base::goodbit;

  const ios_base::openmode ios_base::app;
  const ios_base::openmode ios_base::ate;
  const ios_base::openmode ios_base::binary;
  const ios_base::openmode ios_base::in;
  const ios_base::openmode ios_base::out;
  const ios_base::openmode ios_base::trunc;

  const ios_base::seekdir ios_base::beg;
  const ios_base::seekdir ios_base::cur;
  const ios_base::seekdir ios_base::end;

  const int ios_base::_S_local_word_size;
  int ios_base::Init::_S_ios_base_init = 0;
  bool ios_base::Init::_S_synced_with_stdio = true;

  ios_base::failure::failure(const string& __str) throw()
  {
    strncpy(_M_name, __str.c_str(), _M_bufsize);
    _M_name[_M_bufsize - 1] = '\0';
  }

  ios_base::failure::~failure() throw()
  { }

  const char*
  ios_base::failure::what() const throw()
  { return _M_name; }

  void
  ios_base::Init::_S_ios_create(bool __sync)
  {
    size_t __out_size = __sync ? 0 : static_cast<size_t>(BUFSIZ);
#ifdef _GLIBCPP_HAVE_ISATTY
    size_t __in_size =
      (__sync || isatty (0)) ? 1 : static_cast<size_t>(BUFSIZ);
#else
    size_t __in_size = 1;
#endif

    // NB: The file globals.cc creates the four standard files
    // with NULL buffers. At this point, we swap out the dummy NULL
    // [io]stream objects and buffers with the real deal.
    new (&buf_cout) stdio_filebuf<char>(stdout, ios_base::out, __out_size);
    new (&buf_cin) stdio_filebuf<char>(stdin, ios_base::in, __in_size);
    new (&buf_cerr) stdio_filebuf<char>(stderr, ios_base::out, __out_size);

    new (&cout) ostream(&buf_cout);
    new (&cin) istream(&buf_cin);
    new (&cerr) ostream(&buf_cerr);
    new (&clog) ostream(&buf_cerr);
    cout.init(&buf_cout);
    cin.init(&buf_cin);
    cerr.init(&buf_cerr);
    clog.init(&buf_cerr);
    cin.tie(&cout);
    cerr.flags(ios_base::unitbuf);
    
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    new (&buf_wcout) stdio_filebuf<wchar_t>(stdout, ios_base::out, __out_size);
    new (&buf_wcin) stdio_filebuf<wchar_t>(stdin, ios_base::in, __in_size);
    new (&buf_wcerr) stdio_filebuf<wchar_t>(stderr, ios_base::out, __out_size);
    new (&wcout) wostream(&buf_wcout);
    new (&wcin) wistream(&buf_wcin);
    new (&wcerr) wostream(&buf_wcerr);
    new (&wclog) wostream(&buf_wcerr);
    wcout.init(&buf_wcout);
    wcin.init(&buf_wcin);
    wcerr.init(&buf_wcerr);
    wclog.init(&buf_wcerr);
    wcin.tie(&wcout);
    wcerr.flags(ios_base::unitbuf);
#endif
  }

  void
  ios_base::Init::_S_ios_destroy()
  {
    // Explicitly call dtors to free any memory that is dynamically
    // allocated by filebuf ctor or member functions, but don't
    // deallocate all memory by calling operator delete.
    buf_cout.~stdio_filebuf();
    buf_cin.~stdio_filebuf();
    buf_cerr.~stdio_filebuf();

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    buf_wcout.~stdio_filebuf();
    buf_wcin.~stdio_filebuf();
    buf_wcerr.~stdio_filebuf();
#endif
  }

  ios_base::Init::Init()
  {
    if (_S_ios_base_init == 0)
      {
	// Standard streams default to synced with "C" operations.
	ios_base::Init::_S_synced_with_stdio = true;
	_S_ios_create(ios_base::Init::_S_synced_with_stdio);
      }
    ++_S_ios_base_init;
  }

  ios_base::Init::~Init()
  {
    if (--_S_ios_base_init == 0)
      _S_ios_destroy();
  } 

  // 27.4.2.5  ios_base storage functions
  int 
  ios_base::xalloc() throw()
  {
    // Implementation note: Initialize top to zero to ensure that
    // initialization occurs before main() is started.
    static _Atomic_word _S_top = 0; 
    return __exchange_and_add(&_S_top, 1) + 4;
  }

  // 27.4.2.5  iword/pword storage
  ios_base::_Words&
  ios_base::_M_grow_words(int ix)
  {
    // Precondition: _M_word_size <= ix
    int newsize = _S_local_word_size;
    _Words* words = _M_local_word;
    if (ix > _S_local_word_size - 1)
      {
	if (ix < numeric_limits<int>::max())
	  {
	    newsize = ix + 1;
	    try
	      { words = new _Words[newsize]; }
	    catch (...)
	      {
		_M_streambuf_state |= badbit;
		if (_M_streambuf_state & _M_exception)
		  __throw_ios_failure("ios_base::_M_grow_words failure");
		return _M_word_zero;
	      }
	    for (int i = 0; i < _M_word_size; i++) 
	      words[i] = _M_word[i];
	    if (_M_word && _M_word != _M_local_word) 
	      {
		delete [] _M_word;
		_M_word = 0;
	      }
	  }
	else
	  {
	    _M_streambuf_state |= badbit;
	    if (_M_streambuf_state & _M_exception)
	      __throw_ios_failure("ios_base::_M_grow_words failure");
	    return _M_word_zero;
	  }
      }
    _M_word = words;
    _M_word_size = newsize;
    return _M_word[ix];
  }
  
  // Called only by basic_ios<>::init.
  void 
  ios_base::_M_init()   
  {
    // NB: May be called more than once
    _M_precision = 6;
    _M_width = 0;
    _M_flags = skipws | dec;
    _M_ios_locale = locale();
  }  
  
  // 27.4.2.3  ios_base locale functions
  locale
  ios_base::imbue(const locale& __loc)
  {
    locale __old = _M_ios_locale;
    _M_ios_locale = __loc;
    _M_call_callbacks(imbue_event);
    return __old;
  }

  ios_base::ios_base() : _M_callbacks(0), _M_word_size(_S_local_word_size),
			 _M_word(_M_local_word)
  {
    // Do nothing: basic_ios::init() does it.  
    // NB: _M_callbacks and _M_word must be zero for non-initialized
    // ios_base to go through ~ios_base gracefully.
  }
  
  // 27.4.2.7  ios_base constructors/destructors
  ios_base::~ios_base()
  {
    _M_call_callbacks(erase_event);
    _M_dispose_callbacks();
    if (_M_word != _M_local_word) 
      {
	delete [] _M_word;
	_M_word = 0;
      }
  }

  void 
  ios_base::register_callback(event_callback __fn, int __index)
  { _M_callbacks = new _Callback_list(__fn, __index, _M_callbacks); }

  void 
  ios_base::_M_call_callbacks(event __e) throw()
  {
    _Callback_list* __p = _M_callbacks;
    while (__p)
      {
	try 
	  { (*__p->_M_fn) (__e, *this, __p->_M_index); } 
	catch (...) 
	  { }
	__p = __p->_M_next;
      }
  }

  void 
  ios_base::_M_dispose_callbacks(void)
  {
    _Callback_list* __p = _M_callbacks;
    while (__p && __p->_M_remove_reference() == 0)
      {
	_Callback_list* __next = __p->_M_next;
	delete __p;
	__p = __next;
      }
    _M_callbacks = 0;
  }

  bool 
  ios_base::sync_with_stdio(bool __sync)
  { 
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
    // 49.  Underspecification of ios_base::sync_with_stdio
    bool __ret = ios_base::Init::_S_synced_with_stdio;
#endif

    // Turn off sync with C FILE* for cin, cout, cerr, clog iff
    // currently synchronized.
    if (!__sync && __ret)
      {
	ios_base::Init::_S_synced_with_stdio = false;
	ios_base::Init::_S_ios_destroy();
	ios_base::Init::_S_ios_create(ios_base::Init::_S_synced_with_stdio);
      }
    return __ret; 
  }
}  // namespace std
