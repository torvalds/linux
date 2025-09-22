// Copyright (C) 2002 Free Software Foundation, Inc.
//  
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.

// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA. 

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// Written by Mark Mitchell, CodeSourcery LLC, <mark@codesourcery.com>

#include <cxxabi.h>

namespace __cxxabiv1 
{
  extern "C"
  int __cxa_guard_acquire (__guard *g) 
  {
    return !*(char *)(g);
  }

  extern "C"
  void __cxa_guard_release (__guard *g)
  {
    *(char *)g = 1;
  }

  extern "C"
  void __cxa_guard_abort (__guard *)
  {
  }
}
