// New abi Support -*- C++ -*-

// Copyright (C) 2000, 2001 Free Software Foundation, Inc.
//  
// This file is part of GNU CC.
//
// GNU CC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.

// GNU CC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with GNU CC; see the file COPYING.  If not, write to
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

// Written by Nathan Sidwell, Codesourcery LLC, <nathan@codesourcery.com>

#include <cxxabi.h>
#include <new>
#include <exception>
#include <exception_defines.h>
#include "unwind-cxx.h"

namespace __cxxabiv1
{
  namespace 
  {
    struct uncatch_exception 
    {
      uncatch_exception ();
      ~uncatch_exception () { __cxa_begin_catch (&p->unwindHeader); }
      
      __cxa_exception *p;
    };

    uncatch_exception::uncatch_exception ()
    {
      __cxa_eh_globals *globals = __cxa_get_globals_fast ();

      p = globals->caughtExceptions;
      p->handlerCount -= 1;
      globals->caughtExceptions = p->nextException;
      globals->uncaughtExceptions += 1;
    }
  }

  // Allocate and construct array.
  extern "C" void *
  __cxa_vec_new(std::size_t element_count,
		std::size_t element_size,
		std::size_t padding_size,
		void (*constructor) (void *),
		void (*destructor) (void *))
  {
    return __cxa_vec_new2(element_count, element_size, padding_size,
			   constructor, destructor,
			   &operator new[], &operator delete []);
  }

  extern "C" void *
  __cxa_vec_new2(std::size_t element_count,
		 std::size_t element_size,
		 std::size_t padding_size,
		 void (*constructor) (void *),
		 void (*destructor) (void *),
		 void *(*alloc) (std::size_t),
		 void (*dealloc) (void *))
  {
    std::size_t size = element_count * element_size + padding_size;
    char *base = static_cast <char *> (alloc (size));
    
    if (padding_size)
      {
	base += padding_size;
	reinterpret_cast <std::size_t *> (base)[-1] = element_count;
      }
    try
      {
	__cxa_vec_ctor(base, element_count, element_size,
		       constructor, destructor);
      }
    catch (...)
      {
	{
	  uncatch_exception ue;
	  dealloc(base - padding_size);
	}
	__throw_exception_again;
      }
    return base;
  }
  
  extern "C" void *
  __cxa_vec_new3(std::size_t element_count,
		 std::size_t element_size,
		 std::size_t padding_size,
		 void (*constructor) (void *),
		 void (*destructor) (void *),
		 void *(*alloc) (std::size_t),
		 void (*dealloc) (void *, std::size_t))
  {
    std::size_t size = element_count * element_size + padding_size;
    char *base = static_cast<char *>(alloc (size));
    
    if (padding_size)
      {
	base += padding_size;
	reinterpret_cast<std::size_t *>(base)[-1] = element_count;
      }
    try
      {
	__cxa_vec_ctor(base, element_count, element_size,
		       constructor, destructor);
      }
    catch (...)
      {
	{
	  uncatch_exception ue;
	  dealloc(base - padding_size, size);
	}
	__throw_exception_again;
      }
    return base;
  }
  
  // Construct array.
  extern "C" void
  __cxa_vec_ctor(void *array_address,
		 std::size_t element_count,
		 std::size_t element_size,
		 void (*constructor) (void *),
		 void (*destructor) (void *))
  {
    std::size_t ix = 0;
    char *ptr = static_cast<char *>(array_address);
    
    try
      {
	if (constructor)
	  for (; ix != element_count; ix++, ptr += element_size)
	    constructor(ptr);
      }
    catch (...)
      {
	{
	  uncatch_exception ue;
	  __cxa_vec_cleanup(array_address, ix, element_size, destructor);
	}
	__throw_exception_again;
      }
  }
  
  // Construct an array by copying.
  extern "C" void
  __cxa_vec_cctor(void *dest_array,
		  void *src_array,
		  std::size_t element_count,
		  std::size_t element_size,
		  void (*constructor) (void *, void *),
		  void (*destructor) (void *))
  {
    std::size_t ix = 0;
    char *dest_ptr = static_cast<char *>(dest_array);
    char *src_ptr = static_cast<char *>(src_array);
    
    try
      {
	if (constructor)
	  for (; ix != element_count; 
	       ix++, src_ptr += element_size, dest_ptr += element_size)
	    constructor(dest_ptr, src_ptr);
      }
    catch (...)
      {
	{
	  uncatch_exception ue;
	  __cxa_vec_cleanup(dest_array, ix, element_size, destructor);
	}
	__throw_exception_again;
      }
  }
  
  // Destruct array.
  extern "C" void
  __cxa_vec_dtor(void *array_address,
		 std::size_t element_count,
		 std::size_t element_size,
		 void (*destructor) (void *))
  {
    if (destructor)
      {
	char *ptr = static_cast<char *>(array_address);
	std::size_t ix = element_count;

	ptr += element_count * element_size;

	try
	  {
	    while (ix--)
	      {
		ptr -= element_size;
		destructor(ptr);
	      }
	  }
	catch (...)
	  {
	    {
	      uncatch_exception ue;
	      __cxa_vec_cleanup(array_address, ix, element_size, destructor);
	    }
	    __throw_exception_again;
	  }
      }
  }

  // Destruct array as a result of throwing an exception.
  // [except.ctor]/3 If a destructor called during stack unwinding
  // exits with an exception, terminate is called.
  extern "C" void
  __cxa_vec_cleanup(void *array_address,
		    std::size_t element_count,
		    std::size_t element_size,
		    void (*destructor) (void *))
  {
    if (destructor)
      {
	char *ptr = static_cast <char *> (array_address);
	std::size_t ix = element_count;

	ptr += element_count * element_size;

	try
	  {
	    while (ix--)
	      {
		ptr -= element_size;
		destructor(ptr);
	      }
	  }
	catch (...)
	  {
	    std::terminate();
	  }
      }
  }

  // Destruct and release array.
  extern "C" void
  __cxa_vec_delete(void *array_address,
		   std::size_t element_size,
		   std::size_t padding_size,
		   void (*destructor) (void *))
  {
    __cxa_vec_delete2(array_address, element_size, padding_size,
		       destructor,
		       &operator delete []);
  }

  extern "C" void
  __cxa_vec_delete2(void *array_address,
		    std::size_t element_size,
		    std::size_t padding_size,
		    void (*destructor) (void *),
		    void (*dealloc) (void *))
  {
    char *base = static_cast<char *>(array_address);
  
    if (padding_size)
      {
	std::size_t element_count = reinterpret_cast<std::size_t *>(base)[-1];
	base -= padding_size;
	try
	  {
	    __cxa_vec_dtor(array_address, element_count, element_size,
			   destructor);
	  }
	catch (...)
	  {
	    {
	      uncatch_exception ue;
	      dealloc(base);
	    }
	    __throw_exception_again;
	  }
      }
    dealloc(base);
  }

  extern "C" void
  __cxa_vec_delete3(void *array_address,
		    std::size_t element_size,
		    std::size_t padding_size,
		     void (*destructor) (void *),
		    void (*dealloc) (void *, std::size_t))
  {
    char *base = static_cast <char *> (array_address);
    std::size_t size = 0;
    
    if (padding_size)
      {
	std::size_t element_count = reinterpret_cast<std::size_t *> (base)[-1];
	base -= padding_size;
	size = element_count * element_size + padding_size;
	try
	  {
	    __cxa_vec_dtor(array_address, element_count, element_size,
			   destructor);
	  }
	catch (...)
	  {
	    {
	      uncatch_exception ue;
	      dealloc(base, size);
	    }
	    __throw_exception_again;
	  }
      }
    dealloc(base, size);
  }
} // namespace __cxxabiv1

