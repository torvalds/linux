// Verbose terminate_handler -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation
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

#include <cstdlib>
#include <cstdio>
#include <exception>
#include <exception_defines.h>
#include <cxxabi.h>

using namespace std;
using namespace abi;

namespace __gnu_cxx
{
  /* A replacement for the standard terminate_handler which prints
   more information about the terminating exception (if any) on
   stderr.  */
  void __verbose_terminate_handler()
  {
    // Make sure there was an exception; terminate is also called for an
    // attempt to rethrow when there is no suitable exception.
    type_info *t = __cxa_current_exception_type();
    if (t)
      {
	char const *name = t->name();
	// Note that "name" is the mangled name.
	
	{
	  int status = -1;
	  char *dem = 0;
	  
	  // Disabled until __cxa_demangle gets the runtime GPL exception.
	  dem = __cxa_demangle(name, 0, 0, &status);

	  printf("terminate called after throwing a `%s'\n", 
		 status == 0 ? dem : name);

	  if (status == 0)
	    free(dem);
	}

	// If the exception is derived from std::exception, we can give more
	// information.
	try { __throw_exception_again; }
#ifdef __EXCEPTIONS
	catch (exception &exc)
	  { fprintf(stderr, "  what(): %s\n", exc.what()); }
#endif
	catch (...) { }
      }
    else
      fprintf(stderr, "terminate called without an active exception\n");
    
    abort();
  }
} // namespace __gnu_cxx
