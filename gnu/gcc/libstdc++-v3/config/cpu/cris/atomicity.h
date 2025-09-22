// Low-level functions for atomic operations: CRIS version  -*- C++ -*-

// Copyright (C) 2001, 2003, 2004, 2005 Free Software Foundation, Inc.
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

#include <ext/atomicity.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  _Atomic_word
  __exchange_and_add(volatile _Atomic_word* __mem, int __val)
  {
    int __tmp;
    _Atomic_word __result;

#if (__CRIS_arch_version >= 10)
    __asm__ __volatile__ (" clearf		\n"
			"0:			\n"
			" move.d %4,%2		\n"
			" move.d [%3],%0	\n"
			" add.d %0,%2		\n"
			" ax			\n"
			" move.d %2,[%3]	\n"
			" bwf 0b		\n"
			" clearf		\n"
			:  "=&r" (__result), "=Q" (*__mem), "=&r" (__tmp)
			: "r" (__mem), "g" (__val), "Q" (*__mem)
			/* The memory clobber must stay, regardless of
			   current uses of this function.  */
			: "memory");
#else
    __asm__ __volatile__ (" move $ccr,$r9	\n"
			" di			\n"
			" move.d %4,%2		\n"
			" move.d [%3],%0	\n"
			" add.d %0,%2		\n"
			" move.d %2,[%3]	\n"
			" move $r9,$ccr		\n"
			:  "=&r" (__result), "=Q" (*__mem), "=&r" (__tmp)
			: "r" (__mem), "g" (__val), "Q" (*__mem)
			: "r9",
			  /* The memory clobber must stay, regardless of
			     current uses of this function.  */
			  "memory");
#endif

    return __result;
  }

  void
  __atomic_add(volatile _Atomic_word* __mem, int __val)
  { __exchange_and_add(__mem, __val); }

_GLIBCXX_END_NAMESPACE
