// Standard iostream objects -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2001, 2002, 2005
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

/** @file iostream
 *  This is a Standard C++ Library header. 
 */

//
// ISO C++ 14882: 27.3  Standard iostream objects
//

#ifndef _GLIBCXX_IOSTREAM
#define _GLIBCXX_IOSTREAM 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <ostream>
#include <istream>

_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @name Standard Stream Objects
   *
   *  The &lt;iostream&gt; header declares the eight <em>standard stream
   *  objects</em>.  For other declarations, see
   *  http://gcc.gnu.org/onlinedocs/libstdc++/27_io/howto.html#10 and the
   *  @link s27_2_iosfwd I/O forward declarations @endlink
   *
   *  They are required by default to cooperate with the global C library's
   *  @c FILE streams, and to be available during program startup and
   *  termination.  For more information, see the HOWTO linked to above.
  */
  //@{
  extern istream cin;		///< Linked to standard input
  extern ostream cout;		///< Linked to standard output
  extern ostream cerr;		///< Linked to standard error (unbuffered)
  extern ostream clog;		///< Linked to standard error (buffered)

#ifdef _GLIBCXX_USE_WCHAR_T
  extern wistream wcin;		///< Linked to standard input
  extern wostream wcout;	///< Linked to standard output
  extern wostream wcerr;	///< Linked to standard error (unbuffered)
  extern wostream wclog;	///< Linked to standard error (buffered)
#endif
  //@}

  // For construction of filebuffers for cout, cin, cerr, clog et. al.
  static ios_base::Init __ioinit;

_GLIBCXX_END_NAMESPACE

#endif /* _GLIBCXX_IOSTREAM */
