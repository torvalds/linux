// Low-level functions for atomic operations: m68k version -*- C++ -*-

// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
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

#if ( defined(__mc68020__) || defined(__mc68030__) \
      || defined(__mc68040__) || defined(__mc68060__) ) \
    && !defined(__mcpu32__)
// These variants support compare-and-swap.

static inline _Atomic_word 
__attribute__ ((__unused__))
__exchange_and_add (volatile _Atomic_word *__mem, int __val)
{
  register _Atomic_word __result = *__mem;
  register _Atomic_word __temp;
  __asm__ __volatile__ ("1: move%.l %0,%1\n\t"
			"add%.l %2,%1\n\t"
			"cas%.l %0,%1,%3\n\t"
			"jne 1b"
			: "=d" (__result), "=&d" (__temp)
			: "d" (__val), "m" (*__mem), "0" (__result)
			: "memory");
  return __result;
}

#elif defined(__rtems__)
  /*
   * TAS/JBNE is unsafe on systems with strict priority-based scheduling.
   * Disable interrupts, which we can do only from supervisor mode.
   */
static inline _Atomic_word
__attribute__ ((__unused__))
__exchange_and_add (volatile _Atomic_word *__mem, int __val)
{
  _Atomic_word __result;
  short __level, __tmpsr;
  __asm__ __volatile__ ("move%.w %%sr,%0\n\tor%.l %0,%1\n\tmove%.w %1,%%sr"
                       : "=d"(__level), "=d"(__tmpsr) : "1"(0x700));

  __result = *__mem;
  *__mem = __result + __val;

  __asm__ __volatile__ ("move%.w %0,%%sr" : : "d"(__level));

  return __result;
}

#else

template <int __inst>
struct __Atomicity_lock
{
  static volatile unsigned char _S_atomicity_lock;
};

template <int __inst>
volatile unsigned char __Atomicity_lock<__inst>::_S_atomicity_lock = 0;

template volatile unsigned char __Atomicity_lock<0>::_S_atomicity_lock;

static inline _Atomic_word 
__attribute__ ((__unused__))
__exchange_and_add (volatile _Atomic_word *__mem, int __val)
{
  _Atomic_word __result;

// bset with no immediate addressing
#if defined(__mcf5200__) || defined(__mcf5300__) || defined(__mcf5400__)
  __asm__ __volatile__("1: bset.b #7,%0@\n\tjbne 1b"
		       : /* no outputs */
		       : "a"(&__Atomicity_lock<0>::_S_atomicity_lock)
		       : "cc", "memory");

// bset with immediate addressing
#elif defined(__mc68000__)
  __asm__ __volatile__("1: bset.b #7,%0\n\tjbne 1b"
		       : "+m"(__Atomicity_lock<0>::_S_atomicity_lock)
		       : /* none */
		       : "cc");

#else // 680x0, cpu32, 5400 support test-and-set.
  __asm__ __volatile__("1: tas %0\n\tjbne 1b"
		       : "+m"(__Atomicity_lock<0>::_S_atomicity_lock)
		       : /* none */
		       : "cc");
#endif

  __result = *__mem;
  *__mem = __result + __val;

  __Atomicity_lock<0>::_S_atomicity_lock = 0;

  return __result;
}

#endif /* TAS / BSET */

static inline void
__attribute__ ((__unused__))
__atomic_add (volatile _Atomic_word* __mem, int __val)
{
  // Careful: using add.l with a memory destination is not
  // architecturally guaranteed to be atomic.
  (void) __exchange_and_add (__mem, __val);
}

#endif /* _BITS_ATOMICITY_H */
