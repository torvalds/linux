// Copyright (C) 2002, 2004, 2006 Free Software Foundation, Inc.
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

// Written by Mark Mitchell, CodeSourcery LLC, <mark@codesourcery.com>
// Thread support written by Jason Merrill, Red Hat Inc. <jason@redhat.com>

#include <bits/c++config.h>
#include <cxxabi.h>
#include <exception>
#include <new>
#include <ext/atomicity.h>
#include <ext/concurrence.h>

// The IA64/generic ABI uses the first byte of the guard variable.
// The ARM EABI uses the least significant bit.

// Thread-safe static local initialization support.
#ifdef __GTHREADS
namespace
{
  // A single mutex controlling all static initializations.
  static __gnu_cxx::__recursive_mutex* static_mutex;  

  typedef char fake_recursive_mutex[sizeof(__gnu_cxx::__recursive_mutex)]
  __attribute__ ((aligned(__alignof__(__gnu_cxx::__recursive_mutex))));
  fake_recursive_mutex fake_mutex;

  static void init()
  { static_mutex =  new (&fake_mutex) __gnu_cxx::__recursive_mutex(); }

  __gnu_cxx::__recursive_mutex&
  get_static_mutex()
  {
    static __gthread_once_t once = __GTHREAD_ONCE_INIT;
    __gthread_once(&once, init);
    return *static_mutex;
  }
}

#ifndef _GLIBCXX_GUARD_TEST_AND_ACQUIRE
inline bool
__test_and_acquire (__cxxabiv1::__guard *g)
{
  bool b = _GLIBCXX_GUARD_TEST (g);
  _GLIBCXX_READ_MEM_BARRIER;
  return b;
}
#define _GLIBCXX_GUARD_TEST_AND_ACQUIRE(G) __test_and_acquire (G)
#endif

#ifndef _GLIBCXX_GUARD_SET_AND_RELEASE
inline void
__set_and_release (__cxxabiv1::__guard *g)
{
  _GLIBCXX_WRITE_MEM_BARRIER;
  _GLIBCXX_GUARD_SET (g);
}
#define _GLIBCXX_GUARD_SET_AND_RELEASE(G) __set_and_release (G)
#endif

#else /* !__GTHREADS */

#undef _GLIBCXX_GUARD_TEST_AND_ACQUIRE
#undef _GLIBCXX_GUARD_SET_AND_RELEASE
#define _GLIBCXX_GUARD_SET_AND_RELEASE(G) _GLIBCXX_GUARD_SET (G)

#endif /* __GTHREADS */

namespace __gnu_cxx
{
  // 6.7[stmt.dcl]/4: If control re-enters the declaration (recursively)
  // while the object is being initialized, the behavior is undefined.

  // Since we already have a library function to handle locking, we might
  // as well check for this situation and throw an exception.
  // We use the second byte of the guard variable to remember that we're
  // in the middle of an initialization.
  class recursive_init_error: public std::exception
  {
  public:
    recursive_init_error() throw() { }
    virtual ~recursive_init_error() throw ();
  };

  recursive_init_error::~recursive_init_error() throw() { }
}

namespace __cxxabiv1 
{
  static inline int
  recursion_push (__guard* g)
  { return ((char *)g)[1]++; }

  static inline void
  recursion_pop (__guard* g)
  { --((char *)g)[1]; }

  static int
  acquire (__guard *g)
  {
    if (_GLIBCXX_GUARD_TEST (g))
      return 0;

    if (recursion_push (g))
      {
#ifdef __EXCEPTIONS
	throw __gnu_cxx::recursive_init_error();
#else
	// Use __builtin_trap so we don't require abort().
	__builtin_trap ();
#endif
      }
    return 1;
  }

  extern "C"
  int __cxa_guard_acquire (__guard *g) 
  {
#ifdef __GTHREADS
    // If the target can reorder loads, we need to insert a read memory
    // barrier so that accesses to the guarded variable happen after the
    // guard test.
    if (_GLIBCXX_GUARD_TEST_AND_ACQUIRE (g))
      return 0;

    if (__gthread_active_p ())
      {
	// Simple wrapper for exception safety.
	struct mutex_wrapper
	{
	  bool unlock;
	  mutex_wrapper() : unlock(true)
	  { get_static_mutex().lock(); }

	  ~mutex_wrapper()
	  {
	    if (unlock)
	      static_mutex->unlock();
	  }
	};

	mutex_wrapper mw;
	if (acquire (g))
	  {
	    mw.unlock = false;
	    return 1;
	  }

	return 0;
      }
#endif

    return acquire (g);
  }

  extern "C"
  void __cxa_guard_abort (__guard *g)
  {
    recursion_pop (g);
#ifdef __GTHREADS
    if (__gthread_active_p ())
      static_mutex->unlock();
#endif
  }

  extern "C"
  void __cxa_guard_release (__guard *g)
  {
    recursion_pop (g);
    _GLIBCXX_GUARD_SET_AND_RELEASE (g);
#ifdef __GTHREADS
    if (__gthread_active_p ())
      static_mutex->unlock();
#endif
  }
}
