/* Low-level functions for atomic operations.  PA-RISC version. -*- C++ -*-
   Copyright 2002, 2004 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

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
  static volatile int _S_atomicity_lock;
};

template <int __inst>
volatile int
__Atomicity_lock<__inst>::_S_atomicity_lock __attribute__ ((aligned (16))) = 1;

/* Because of the lack of weak support when using the hpux
   som linker, we explicitly instantiate the atomicity lock
   in src/misc-inst.cc when _GLIBCPP_INST_ATOMICITY_LOCK
   is defined.  */
#ifndef _GLIBCPP_INST_ATOMICITY_LOCK
template volatile int __Atomicity_lock<0>::_S_atomicity_lock;
#endif

static inline int
__attribute__ ((__unused__))
__exchange_and_add (volatile _Atomic_word* __mem, int __val)
{
  _Atomic_word result;
  int tmp;
  volatile int& lock = __Atomicity_lock<0>::_S_atomicity_lock;

  __asm__ __volatile__ ("ldcw 0(%1),%0\n\t"
			"cmpib,<>,n 0,%0,.+20\n\t"
			"ldw 0(%1),%0\n\t"
			"cmpib,= 0,%0,.-4\n\t"
			"nop\n\t"
			"b,n .-20"
			: "=&r" (tmp)
			: "r" (&lock));

  result = *__mem;
  *__mem = result + __val;
  /* Reset lock with PA 2.0 "ordered" store.  */
  __asm__ __volatile__ ("stw,ma %1,0(%0)"
			: : "r" (&lock), "r" (tmp) : "memory");
  return result;
}

static inline void
__attribute__ ((__unused__))
__atomic_add (_Atomic_word* __mem, int __val)
{
  int tmp;
  volatile int& lock = __Atomicity_lock<0>::_S_atomicity_lock;

  __asm__ __volatile__ ("ldcw 0(%1),%0\n\t"
			"cmpib,<>,n 0,%0,.+20\n\t"
			"ldw 0(%1),%0\n\t"
			"cmpib,= 0,%0,.-4\n\t"
			"nop\n\t"
			"b,n .-20"
			: "=&r" (tmp)
			: "r" (&lock));

  *__mem += __val;
  /* Reset lock with PA 2.0 "ordered" store.  */
  __asm__ __volatile__ ("stw,ma %1,0(%0)"
			: : "r" (&lock), "r" (tmp) : "memory");
}

#endif
