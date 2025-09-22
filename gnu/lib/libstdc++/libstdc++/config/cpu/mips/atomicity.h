// Low-level functions for atomic operations.

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

#ifndef _BITS_ATOMICITY_H
#define _BITS_ATOMICITY_H 1

typedef int _Atomic_word;

static inline int
__attribute__ ((__unused__))
__exchange_and_add (volatile _Atomic_word *__mem, int __val)
{
  int __result, __tmp;

  __asm__ __volatile__
    ("/* Inline exchange & add */\n\t"
     "1:\n\t"
     ".set	push\n\t"
     ".set	mips2\n\t"
     "ll	%0,%3\n\t"
     "addu	%1,%4,%0\n\t"
     "sc	%1,%2\n\t"
     ".set	pop\n\t"
     "beqz	%1,1b\n\t"
     "/* End exchange & add */"
     : "=&r"(__result), "=&r"(__tmp), "=m"(*__mem)
     : "m" (*__mem), "r"(__val)
     : "memory");

  return __result;
}

static inline void
__attribute__ ((__unused__))
__atomic_add (volatile _Atomic_word *__mem, int __val)
{
  int __result;

  __asm__ __volatile__
    ("/* Inline atomic add */\n\t"
     "1:\n\t"
     ".set	push\n\t"
     ".set	mips2\n\t"
     "ll	%0,%2\n\t"
     "addu	%0,%3,%0\n\t"
     "sc	%0,%1\n\t"
     ".set	pop\n\t"
     "beqz	%0,1b\n\t"
     "/* End atomic add */"
     : "=&r"(__result), "=m"(*__mem)
     : "m" (*__mem), "r"(__val)
     : "memory");
}

#endif /* atomicity.h */
