// Support routines for the -*- C++ -*- dynamic memory management.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2004
// Free Software Foundation
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING.  If not, write to
// the Free Software Foundation, 51 Franklin Street, Fifth Floor,
// Boston, MA 02110-1301, USA.
//
// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <bits/c++config.h>
#include <cstdlib>
#include <exception_defines.h>
#include "new"

using std::new_handler;
using std::bad_alloc;
#if _GLIBCXX_HOSTED
using std::malloc;
#else
// A freestanding C runtime may not provide "malloc" -- but there is no
// other reasonable way to implement "operator new".
extern "C" void *malloc (std::size_t);
#endif

extern new_handler __new_handler;

_GLIBCXX_WEAK_DEFINITION void *
operator new (std::size_t sz) throw (std::bad_alloc)
{
  void *p;

  /* malloc (0) is unpredictable; avoid it.  */
  if (sz == 0)
    sz = 1;
  p = (void *) malloc (sz);
  while (p == 0)
    {
      new_handler handler = __new_handler;
      if (! handler)
#ifdef __EXCEPTIONS
	throw bad_alloc();
#else
        std::abort();
#endif
      handler ();
      p = (void *) malloc (sz);
    }

  return p;
}
