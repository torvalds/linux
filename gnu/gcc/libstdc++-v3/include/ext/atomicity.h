// Support for atomic operations -*- C++ -*-

// Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.
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

/** @file atomicity.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _GLIBCXX_ATOMICITY_H
#define _GLIBCXX_ATOMICITY_H	1

#include <bits/c++config.h>
#include <bits/gthr.h>
#include <bits/atomic_word.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  // Functions for portable atomic access.
  // To abstract locking primatives across all thread policies, use:
  // __exchange_and_add_dispatch
  // __atomic_add_dispatch
#ifdef _GLIBCXX_ATOMIC_BUILTINS
  static inline _Atomic_word 
  __exchange_and_add(volatile _Atomic_word* __mem, int __val)
  { return __sync_fetch_and_add(__mem, __val); }

  static inline void
  __atomic_add(volatile _Atomic_word* __mem, int __val)
  { __sync_fetch_and_add(__mem, __val); }
#else
  _Atomic_word
  __attribute__ ((__unused__))
  __exchange_and_add(volatile _Atomic_word*, int);

  void
  __attribute__ ((__unused__))
  __atomic_add(volatile _Atomic_word*, int);
#endif

  static inline _Atomic_word
  __exchange_and_add_single(_Atomic_word* __mem, int __val)
  {
    _Atomic_word __result = *__mem;
    *__mem += __val;
    return __result;
  }

  static inline void
  __atomic_add_single(_Atomic_word* __mem, int __val)
  { *__mem += __val; }

  static inline _Atomic_word
  __attribute__ ((__unused__))
  __exchange_and_add_dispatch(_Atomic_word* __mem, int __val)
  {
#ifdef __GTHREADS
    if (__gthread_active_p())
      return __exchange_and_add(__mem, __val);
    else
      return __exchange_and_add_single(__mem, __val);
#else
    return __exchange_and_add_single(__mem, __val);
#endif
  }

  static inline void
  __attribute__ ((__unused__))
  __atomic_add_dispatch(_Atomic_word* __mem, int __val)
  {
#ifdef __GTHREADS
    if (__gthread_active_p())
      __atomic_add(__mem, __val);
    else
      __atomic_add_single(__mem, __val);
#else
    __atomic_add_single(__mem, __val);
#endif
  }

_GLIBCXX_END_NAMESPACE

// Even if the CPU doesn't need a memory barrier, we need to ensure
// that the compiler doesn't reorder memory accesses across the
// barriers.
#ifndef _GLIBCXX_READ_MEM_BARRIER
#define _GLIBCXX_READ_MEM_BARRIER __asm __volatile ("":::"memory")
#endif
#ifndef _GLIBCXX_WRITE_MEM_BARRIER
#define _GLIBCXX_WRITE_MEM_BARRIER __asm __volatile ("":::"memory")
#endif

#endif 
