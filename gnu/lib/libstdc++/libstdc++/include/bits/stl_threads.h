// Threading support -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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

/*
 * Copyright (c) 1997-1999
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/** @file stl_threads.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __SGI_STL_INTERNAL_THREADS_H
#define __SGI_STL_INTERNAL_THREADS_H

// The only supported threading model is GCC's own gthr.h abstraction layer.
#include "bits/gthr.h"

namespace std
{
  // Class _Refcount_Base provides a type, _RC_t, a data member,
  // _M_ref_count, and member functions _M_incr and _M_decr, which perform
  // atomic preincrement/predecrement.  The constructor initializes 
  // _M_ref_count.
  struct _Refcount_Base
  {
    // The type _RC_t
    typedef size_t _RC_t;
    
    // The data member _M_ref_count
    volatile _RC_t _M_ref_count;
    
    // Constructor
    __gthread_mutex_t _M_ref_count_lock;

    _Refcount_Base(_RC_t __n) : _M_ref_count(__n)
    {
#ifdef __GTHREAD_MUTEX_INIT
      __gthread_mutex_t __tmp = __GTHREAD_MUTEX_INIT;
      _M_ref_count_lock = __tmp;
#elif defined(__GTHREAD_MUTEX_INIT_FUNCTION)
      __GTHREAD_MUTEX_INIT_FUNCTION (&_M_ref_count_lock);
#else
#error __GTHREAD_MUTEX_INIT or __GTHREAD_MUTEX_INIT_FUNCTION should be defined by gthr.h abstraction layer, report problem to libstdc++@gcc.gnu.org.
#endif
    }

    void 
    _M_incr() 
    {
      __gthread_mutex_lock(&_M_ref_count_lock);
      ++_M_ref_count;
      __gthread_mutex_unlock(&_M_ref_count_lock);
    }

    _RC_t 
    _M_decr() 
    {
      __gthread_mutex_lock(&_M_ref_count_lock);
      volatile _RC_t __tmp = --_M_ref_count;
      __gthread_mutex_unlock(&_M_ref_count_lock);
      return __tmp;
    }
  };

  // Atomic swap on unsigned long
  // This is guaranteed to behave as though it were atomic only if all
  // possibly concurrent updates use _Atomic_swap.
  // In some cases the operation is emulated with a lock.
#if defined (__GTHREAD_MUTEX_INIT)
  // This could be optimized to use the atomicity.h abstraction layer.
  // vyzo: simple _Atomic_swap implementation following the guidelines above
  // We use a template here only to get a unique initialized instance.
  template<int __dummy>
    struct _Swap_lock_struct 
    { static __gthread_mutex_t _S_swap_lock; };

  template<int __dummy>
    __gthread_mutex_t
    _Swap_lock_struct<__dummy>::_S_swap_lock = __GTHREAD_MUTEX_INIT;

  // This should be portable, but performance is expected to be quite
  // awful.  This really needs platform specific code.
  inline unsigned long 
  _Atomic_swap(unsigned long * __p, unsigned long __q) 
  {
    __gthread_mutex_lock(&_Swap_lock_struct<0>::_S_swap_lock);
    unsigned long __result = *__p;
    *__p = __q;
    __gthread_mutex_unlock(&_Swap_lock_struct<0>::_S_swap_lock);
    return __result;
  }
#endif
} //namespace std

  // Locking class.  Note that this class *does not have a
  // constructor*.  It must be initialized either statically, with
  // __STL_MUTEX_INITIALIZER, or dynamically, by explicitly calling
  // the _M_initialize member function.  (This is similar to the ways
  // that a pthreads mutex can be initialized.)  There are explicit
  // member functions for acquiring and releasing the lock.

  // There is no constructor because static initialization is
  // essential for some uses, and only a class aggregate (see section
  // 8.5.1 of the C++ standard) can be initialized that way.  That
  // means we must have no constructors, no base classes, no virtual
  // functions, and no private or protected members.

#if !defined(__GTHREAD_MUTEX_INIT) && defined(__GTHREAD_MUTEX_INIT_FUNCTION)
namespace __gnu_cxx
{
  extern __gthread_mutex_t _GLIBCPP_mutex;
  extern __gthread_mutex_t *_GLIBCPP_mutex_address;
  extern __gthread_once_t _GLIBCPP_once;
  extern void _GLIBCPP_mutex_init (void);
  extern void _GLIBCPP_mutex_address_init (void);
}
#endif

namespace std
{
  struct _STL_mutex_lock
  {
    // The class must be statically initialized with __STL_MUTEX_INITIALIZER.
#if !defined(__GTHREAD_MUTEX_INIT) && defined(__GTHREAD_MUTEX_INIT_FUNCTION)
    volatile int _M_init_flag;
    __gthread_once_t _M_once;
#endif
    __gthread_mutex_t _M_lock;

    void 
    _M_initialize() 
    {
#ifdef __GTHREAD_MUTEX_INIT
      // There should be no code in this path given the usage rules above.
#elif defined(__GTHREAD_MUTEX_INIT_FUNCTION)
      if (_M_init_flag) return;
      if (__gthread_once (&__gnu_cxx::_GLIBCPP_once,
			  __gnu_cxx::_GLIBCPP_mutex_init) != 0
	  && __gthread_active_p ())
	abort ();
      __gthread_mutex_lock (&__gnu_cxx::_GLIBCPP_mutex);
      if (!_M_init_flag) 
	{
	  // Even though we have a global lock, we use __gthread_once to be
	  // absolutely certain the _M_lock mutex is only initialized once on
	  // multiprocessor systems.
	  __gnu_cxx::_GLIBCPP_mutex_address = &_M_lock;
	  if (__gthread_once (&_M_once,
			      __gnu_cxx::_GLIBCPP_mutex_address_init) != 0
	    && __gthread_active_p ())
	    abort ();
	  _M_init_flag = 1;
	}
      __gthread_mutex_unlock (&__gnu_cxx::_GLIBCPP_mutex);
#endif
    }

    void 
    _M_acquire_lock() 
    {
#if !defined(__GTHREAD_MUTEX_INIT) && defined(__GTHREAD_MUTEX_INIT_FUNCTION)
      if (!_M_init_flag) _M_initialize();
#endif
      __gthread_mutex_lock(&_M_lock);
    }

    void 
    _M_release_lock() 
    {
#if !defined(__GTHREAD_MUTEX_INIT) && defined(__GTHREAD_MUTEX_INIT_FUNCTION)
      if (!_M_init_flag) _M_initialize();
#endif
      __gthread_mutex_unlock(&_M_lock);
    }
  };
  
#ifdef __GTHREAD_MUTEX_INIT
#define __STL_MUTEX_INITIALIZER = { __GTHREAD_MUTEX_INIT }
#elif defined(__GTHREAD_MUTEX_INIT_FUNCTION)
#ifdef __GTHREAD_MUTEX_INIT_DEFAULT
#define __STL_MUTEX_INITIALIZER \
  = { 0, __GTHREAD_ONCE_INIT, __GTHREAD_MUTEX_INIT_DEFAULT }
#else
#define __STL_MUTEX_INITIALIZER = { 0, __GTHREAD_ONCE_INIT }
#endif
#endif

  // A locking class that uses _STL_mutex_lock.  The constructor takes a
  // reference to an _STL_mutex_lock, and acquires a lock.  The
  // destructor releases the lock.  It's not clear that this is exactly
  // the right functionality.  It will probably change in the future.
  struct _STL_auto_lock
  {
    _STL_mutex_lock& _M_lock;
    
    _STL_auto_lock(_STL_mutex_lock& __lock) : _M_lock(__lock)
    { _M_lock._M_acquire_lock(); }

    ~_STL_auto_lock() { _M_lock._M_release_lock(); }

  private:
    void operator=(const _STL_auto_lock&);
    _STL_auto_lock(const _STL_auto_lock&);
  };
  
} // namespace std

#endif 
