// Locale support -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2003 Free Software Foundation, Inc.
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
  
// Information extracted from target/h/ctype.h.
  
_GLIBCXX_BEGIN_NAMESPACE(std)

  /// @brief  Base class for ctype.
  struct ctype_base
  {
    // Non-standard typedefs.
    typedef const unsigned char* 	__to_type;

    // NB: Offsets into ctype<char>::_M_table force a particular size
    // on the mask type. Because of this, we don't use an enum.
    typedef unsigned char 	mask;   
    static const mask upper    	= _C_UPPER;
    static const mask lower 	= _C_LOWER;
    static const mask alpha 	= _C_UPPER | _C_LOWER;
    static const mask digit 	= _C_NUMBER;
    static const mask xdigit 	= _C_HEX_NUMBER;
    static const mask space 	= _C_WHITE_SPACE | _C_CONTROL;
    static const mask print 	= (_C_UPPER | _C_LOWER | _C_NUMBER
				   | _C_WHITE_SPACE | _C_PUNCT);
    static const mask graph 	= _C_UPPER | _C_LOWER | _C_NUMBER | _C_PUNCT;
    static const mask cntrl 	= _C_CONTROL;
    static const mask punct 	= _C_PUNCT;
    static const mask alnum 	= _C_UPPER | _C_LOWER | _C_NUMBER;
  };

_GLIBCXX_END_NAMESPACE
