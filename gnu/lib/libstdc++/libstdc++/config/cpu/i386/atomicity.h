// Low-level functions for atomic operations: x86, x >= 3 version  -*- C++ -*-

// Copyright (C) 2003 Free Software Foundation, Inc.
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

#ifndef _BITS_ATOMICITY_H
#define _BITS_ATOMICITY_H	1

typedef int _Atomic_word;

template <int __inst>
struct __Atomicity_lock
{
  static volatile _Atomic_word _S_atomicity_lock;
};

template <int __inst>
volatile _Atomic_word __Atomicity_lock<__inst>::_S_atomicity_lock = 0;

template volatile _Atomic_word __Atomicity_lock<0>::_S_atomicity_lock;

static inline _Atomic_word 
__attribute__ ((__unused__))
__exchange_and_add (volatile _Atomic_word *__mem, int __val)
{
  register _Atomic_word __result, __tmp = 1;

  /* obtain the atomic exchange/add spin lock */
  do {
    __asm__ __volatile__ ("xchg{l} {%0,%1|%1,%0}"
			  : "+m" (__Atomicity_lock<0>::_S_atomicity_lock),
			    "+r" (__tmp));
  } while (__tmp);

  __result = *__mem;
  *__mem += __val;

  /* release spin lock */
  __Atomicity_lock<0>::_S_atomicity_lock = 0;

  return __result;
}

static inline void
__attribute__ ((__unused__))
__atomic_add (volatile _Atomic_word* __mem, int __val)
{
  __exchange_and_add (__mem, __val);
}

#endif /* atomicity.h */
