// Iostreams base classes -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

//
// ISO C++ 14882: 27.4  Iostreams base classes
//

#include <ios>
#include <ostream>
#include <istream>
#include <fstream>
#include <ext/stdio_filebuf.h>
#include <ext/stdio_sync_filebuf.h>

namespace __gnu_internal _GLIBCXX_VISIBILITY(hidden)
{
  using namespace __gnu_cxx;

  // Extern declarations for global objects in src/globals.cc.
  extern stdio_sync_filebuf<char> buf_cout_sync;
  extern stdio_sync_filebuf<char> buf_cin_sync;
  extern stdio_sync_filebuf<char> buf_cerr_sync;

  extern stdio_filebuf<char> buf_cout;
  extern stdio_filebuf<char> buf_cin;
  extern stdio_filebuf<char> buf_cerr;

#ifdef _GLIBCXX_USE_WCHAR_T
  extern stdio_sync_filebuf<wchar_t> buf_wcout_sync;
  extern stdio_sync_filebuf<wchar_t> buf_wcin_sync;
  extern stdio_sync_filebuf<wchar_t> buf_wcerr_sync;

  extern stdio_filebuf<wchar_t> buf_wcout;
  extern stdio_filebuf<wchar_t> buf_wcin;
  extern stdio_filebuf<wchar_t> buf_wcerr;
#endif
} // namespace __gnu_internal

_GLIBCXX_BEGIN_NAMESPACE(std)

  using namespace __gnu_internal;
  
  extern istream cin;
  extern ostream cout;
  extern ostream cerr;
  extern ostream clog;

#ifdef _GLIBCXX_USE_WCHAR_T
  extern wistream wcin;
  extern wostream wcout;
  extern wostream wcerr;
  extern wostream wclog;
#endif

  ios_base::Init::Init()
  {
    if (__gnu_cxx::__exchange_and_add_dispatch(&_S_refcount, 1) == 0)
      {
	// Standard streams default to synced with "C" operations.
	_S_synced_with_stdio = true;

	new (&buf_cout_sync) stdio_sync_filebuf<char>(stdout);
	new (&buf_cin_sync) stdio_sync_filebuf<char>(stdin);
	new (&buf_cerr_sync) stdio_sync_filebuf<char>(stderr);

	// The standard streams are constructed once only and never
	// destroyed.
	new (&cout) ostream(&buf_cout_sync);
	new (&cin) istream(&buf_cin_sync);
	new (&cerr) ostream(&buf_cerr_sync);
	new (&clog) ostream(&buf_cerr_sync);
	cin.tie(&cout);
	cerr.flags(ios_base::unitbuf);
	// _GLIBCXX_RESOLVE_LIB_DEFECTS
	// 455. cerr::tie() and wcerr::tie() are overspecified.
	cerr.tie(&cout);

#ifdef _GLIBCXX_USE_WCHAR_T
	new (&buf_wcout_sync) stdio_sync_filebuf<wchar_t>(stdout);
	new (&buf_wcin_sync) stdio_sync_filebuf<wchar_t>(stdin);
	new (&buf_wcerr_sync) stdio_sync_filebuf<wchar_t>(stderr);

	new (&wcout) wostream(&buf_wcout_sync);
	new (&wcin) wistream(&buf_wcin_sync);
	new (&wcerr) wostream(&buf_wcerr_sync);
	new (&wclog) wostream(&buf_wcerr_sync);
	wcin.tie(&wcout);
	wcerr.flags(ios_base::unitbuf);
	wcerr.tie(&wcout);	
#endif
	
	// NB: Have to set refcount above one, so that standard
	// streams are not re-initialized with uses of ios_base::Init
	// besides <iostream> static object, ie just using <ios> with
	// ios_base::Init objects.
	__gnu_cxx::__atomic_add_dispatch(&_S_refcount, 1);
      }
  }

  ios_base::Init::~Init()
  {
    if (__gnu_cxx::__exchange_and_add_dispatch(&_S_refcount, -1) == 2)
      {
	// Catch any exceptions thrown by basic_ostream::flush()
	try
	  { 
	    // Flush standard output streams as required by 27.4.2.1.6
	    cout.flush();
	    cerr.flush();
	    clog.flush();
    
#ifdef _GLIBCXX_USE_WCHAR_T
	    wcout.flush();
	    wcerr.flush();
	    wclog.flush();    
#endif
	  }
	catch (...)
	  { }
      }
  } 

  bool 
  ios_base::sync_with_stdio(bool __sync)
  { 
    // _GLIBCXX_RESOLVE_LIB_DEFECTS
    // 49.  Underspecification of ios_base::sync_with_stdio
    bool __ret = ios_base::Init::_S_synced_with_stdio;

    // Turn off sync with C FILE* for cin, cout, cerr, clog iff
    // currently synchronized.
    if (!__sync && __ret)
      {
	// Make sure the standard streams are constructed.
	ios_base::Init __init;

	ios_base::Init::_S_synced_with_stdio = __sync;

	// Explicitly call dtors to free any memory that is
	// dynamically allocated by filebuf ctor or member functions,
	// but don't deallocate all memory by calling operator delete.
	buf_cout_sync.~stdio_sync_filebuf<char>();
	buf_cin_sync.~stdio_sync_filebuf<char>();
	buf_cerr_sync.~stdio_sync_filebuf<char>();

#ifdef _GLIBCXX_USE_WCHAR_T
	buf_wcout_sync.~stdio_sync_filebuf<wchar_t>();
	buf_wcin_sync.~stdio_sync_filebuf<wchar_t>();
	buf_wcerr_sync.~stdio_sync_filebuf<wchar_t>();
#endif

	// Create stream buffers for the standard streams and use
	// those buffers without destroying and recreating the
	// streams.
	new (&buf_cout) stdio_filebuf<char>(stdout, ios_base::out);
	new (&buf_cin) stdio_filebuf<char>(stdin, ios_base::in);
	new (&buf_cerr) stdio_filebuf<char>(stderr, ios_base::out);
	cout.rdbuf(&buf_cout);
	cin.rdbuf(&buf_cin);
	cerr.rdbuf(&buf_cerr);
	clog.rdbuf(&buf_cerr);
    
#ifdef _GLIBCXX_USE_WCHAR_T
	new (&buf_wcout) stdio_filebuf<wchar_t>(stdout, ios_base::out);
	new (&buf_wcin) stdio_filebuf<wchar_t>(stdin, ios_base::in);
	new (&buf_wcerr) stdio_filebuf<wchar_t>(stderr, ios_base::out);
	wcout.rdbuf(&buf_wcout);
	wcin.rdbuf(&buf_wcin);
	wcerr.rdbuf(&buf_wcerr);
	wclog.rdbuf(&buf_wcerr);
#endif
      }
    return __ret; 
  }

_GLIBCXX_END_NAMESPACE
