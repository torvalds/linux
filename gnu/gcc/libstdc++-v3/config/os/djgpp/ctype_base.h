// Locale support -*- C++ -*-

// Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.1  Locales
//
  
_GLIBCXX_BEGIN_NAMESPACE(std)

  /// @brief  Base class for ctype.
  struct ctype_base
  {
    typedef unsigned short 	mask;
    
    // Non-standard typedefs.
    typedef unsigned char *     __to_type;

    // NB: Offsets into ctype<char>::_M_table force a particular size
    // on the mask type. Because of this, we don't use an enum.
    static const mask space = __dj_ISSPACE;	// Whitespace
    static const mask print = __dj_ISPRINT;	// Printing
    static const mask cntrl = __dj_ISCNTRL;	// Control character
    static const mask upper = __dj_ISUPPER;	// UPPERCASE
    static const mask lower = __dj_ISLOWER;	// lowercase
    static const mask alpha = __dj_ISALPHA;	// Alphabetic
    static const mask digit = __dj_ISDIGIT;	// Numeric
    static const mask punct = __dj_ISPUNCT;     // Punctuation
    static const mask xdigit = __dj_ISXDIGIT;   // Hexadecimal numeric
    static const mask alnum = __dj_ISALPHA | __dj_ISDIGIT;  // Alphanumeric
    static const mask graph = __dj_ISALPHA | __dj_ISDIGIT | __dj_ISPUNCT;  // Graphical
  };

_GLIBCXX_END_NAMESPACE
