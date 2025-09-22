// -*- C++ -*- Exception handling routines for throwing.
// Copyright (C) 2001, 2003 Free Software Foundation, Inc.
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

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <bits/c++config.h>
#include "unwind-cxx.h"

using namespace __cxxabiv1;


static void
__gxx_exception_cleanup (_Unwind_Reason_Code code, _Unwind_Exception *exc)
{
  __cxa_exception *header = __get_exception_header_from_ue (exc);

  // If we haven't been caught by a foreign handler, then this is
  // some sort of unwind error.  In that case just die immediately.
  // _Unwind_DeleteException in the HP-UX IA64 libunwind library
  //  returns _URC_NO_REASON and not _URC_FOREIGN_EXCEPTION_CAUGHT
  // like the GCC _Unwind_DeleteException function does.
  if (code != _URC_FOREIGN_EXCEPTION_CAUGHT && code != _URC_NO_REASON)
    __terminate (header->terminateHandler);

  if (header->exceptionDestructor)
    header->exceptionDestructor (header + 1);

  __cxa_free_exception (header + 1);
}


extern "C" void
__cxxabiv1::__cxa_throw (void *obj, std::type_info *tinfo, 
			 void (*dest) (void *))
{
  __cxa_exception *header = __get_exception_header_from_obj (obj);
  header->exceptionType = tinfo;
  header->exceptionDestructor = dest;
  header->unexpectedHandler = __unexpected_handler;
  header->terminateHandler = __terminate_handler;
  __GXX_INIT_EXCEPTION_CLASS(header->unwindHeader.exception_class);
  header->unwindHeader.exception_cleanup = __gxx_exception_cleanup;

#ifdef _GLIBCXX_SJLJ_EXCEPTIONS
  _Unwind_SjLj_RaiseException (&header->unwindHeader);
#else
  _Unwind_RaiseException (&header->unwindHeader);
#endif

  // Some sort of unwinding error.  Note that terminate is a handler.
  __cxa_begin_catch (&header->unwindHeader);
  std::terminate ();
}

extern "C" void
__cxxabiv1::__cxa_rethrow ()
{
  __cxa_eh_globals *globals = __cxa_get_globals ();
  __cxa_exception *header = globals->caughtExceptions;

  globals->uncaughtExceptions += 1;

  // Watch for luser rethrowing with no active exception.
  if (header)
    {
      // Tell __cxa_end_catch this is a rethrow.
      if (!__is_gxx_exception_class(header->unwindHeader.exception_class))
	globals->caughtExceptions = 0;
      else
	header->handlerCount = -header->handlerCount;

#ifdef _GLIBCXX_SJLJ_EXCEPTIONS
      _Unwind_SjLj_Resume_or_Rethrow (&header->unwindHeader);
#else
#if defined(_LIBUNWIND_STD_ABI)
      _Unwind_RaiseException (&header->unwindHeader);
#else
      _Unwind_Resume_or_Rethrow (&header->unwindHeader);
#endif
#endif
  
      // Some sort of unwinding error.  Note that terminate is a handler.
      __cxa_begin_catch (&header->unwindHeader);
    }
  std::terminate ();
}
