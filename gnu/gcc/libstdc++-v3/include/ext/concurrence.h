// Support for concurrent programing -*- C++ -*-

// Copyright (C) 2003, 2004, 2005, 2006
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

/** @file concurrence.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CONCURRENCE_H
#define _CONCURRENCE_H 1

#include <cstdlib>
#include <exception>
#include <bits/gthr.h> 
#include <bits/functexcept.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  // Available locking policies:
  // _S_single    single-threaded code that doesn't need to be locked.
  // _S_mutex     multi-threaded code that requires additional support
  //              from gthr.h or abstraction layers in concurrance.h.
  // _S_atomic    multi-threaded code using atomic operations.
  enum _Lock_policy { _S_single, _S_mutex, _S_atomic }; 

  // Compile time constant that indicates prefered locking policy in
  // the current configuration.
  static const _Lock_policy __default_lock_policy = 
#ifdef __GTHREADS
  // NB: This macro doesn't actually exist yet in the compiler, but is
  // set somewhat haphazardly at configure time.
#ifdef _GLIBCXX_ATOMIC_BUILTINS
  _S_atomic;
#else
  _S_mutex;
#endif
#else
  _S_single;
#endif
  
  // NB: As this is used in libsupc++, need to only depend on
  // exception. No stdexception classes, no use of std::string.
  class __concurrence_lock_error : public std::exception
  {
  public:
    virtual char const*
    what() const throw()
    { return "__gnu_cxx::__concurrence_lock_error"; }
  };

  class __concurrence_unlock_error : public std::exception
  {
  public:
    virtual char const*
    what() const throw()
    { return "__gnu_cxx::__concurrence_unlock_error"; }
  };

  // Substitute for concurrence_error object in the case of -fno-exceptions.
  inline void
  __throw_concurrence_lock_error()
  {
#if __EXCEPTIONS
    throw __concurrence_lock_error();
#else
    std::abort();
#endif
  }

  inline void
  __throw_concurrence_unlock_error()
  {
#if __EXCEPTIONS
    throw __concurrence_unlock_error();
#else
    std::abort();
#endif
  }

  class __mutex 
  {
  private:
    __gthread_mutex_t _M_mutex;

    __mutex(const __mutex&);
    __mutex& operator=(const __mutex&);

  public:
    __mutex() 
    { 
#if __GTHREADS
      if (__gthread_active_p())
	{
#if defined __GTHREAD_MUTEX_INIT
	  __gthread_mutex_t __tmp = __GTHREAD_MUTEX_INIT;
	  _M_mutex = __tmp;
#else
	  __GTHREAD_MUTEX_INIT_FUNCTION(&_M_mutex); 
#endif
	}
#endif 
    }

    void lock()
    {
#if __GTHREADS
      if (__gthread_active_p())
	{
	  if (__gthread_mutex_lock(&_M_mutex) != 0)
	    __throw_concurrence_lock_error();
	}
#endif
    }
    
    void unlock()
    {
#if __GTHREADS
      if (__gthread_active_p())
	{
	  if (__gthread_mutex_unlock(&_M_mutex) != 0)
	    __throw_concurrence_unlock_error();
	}
#endif
    }
  };

  class __recursive_mutex 
  {
  private:
    __gthread_recursive_mutex_t _M_mutex;

    __recursive_mutex(const __recursive_mutex&);
    __recursive_mutex& operator=(const __recursive_mutex&);

  public:
    __recursive_mutex() 
    { 
#if __GTHREADS
      if (__gthread_active_p())
	{
#if defined __GTHREAD_RECURSIVE_MUTEX_INIT
	  __gthread_recursive_mutex_t __tmp = __GTHREAD_RECURSIVE_MUTEX_INIT;
	  _M_mutex = __tmp;
#else
	  __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION(&_M_mutex); 
#endif
	}
#endif 
    }

    void lock()
    { 
#if __GTHREADS
      if (__gthread_active_p())
	{
	  if (__gthread_recursive_mutex_lock(&_M_mutex) != 0)
	    __throw_concurrence_lock_error();
	}
#endif
    }
    
    void unlock()
    { 
#if __GTHREADS
      if (__gthread_active_p())
	{
	  if (__gthread_recursive_mutex_unlock(&_M_mutex) != 0)
	    __throw_concurrence_unlock_error();
	}
#endif
    }
  };

  /// @brief  Scoped lock idiom.
  // Acquire the mutex here with a constructor call, then release with
  // the destructor call in accordance with RAII style.
  class __scoped_lock
  {
  public:
    typedef __mutex __mutex_type;

  private:
    __mutex_type& _M_device;

    __scoped_lock(const __scoped_lock&);
    __scoped_lock& operator=(const __scoped_lock&);

  public:
    explicit __scoped_lock(__mutex_type& __name) : _M_device(__name)
    { _M_device.lock(); }

    ~__scoped_lock() throw()
    { _M_device.unlock(); }
  };

_GLIBCXX_END_NAMESPACE

#endif
