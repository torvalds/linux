// Low-level functions for atomic operations: Sparc version  -*- C++ -*-

// Copyright (C) 1999, 2000, 2001, 2002, 2004, 2005
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

#include <ext/atomicity.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

#ifdef __arch64__
  _Atomic_word
  __attribute__ ((__unused__))
  __exchange_and_add(volatile _Atomic_word* __mem, int __val)
  {
    _Atomic_word __tmp1, __tmp2;
    _Atomic_word __val_extended = __val;

    __asm__ __volatile__("1:	ldx	[%3], %0\n\t"
			 "	add	%0, %4, %1\n\t"
			 "	casx	[%3], %0, %1\n\t"
			 "	sub	%0, %1, %0\n\t"
			 "	brnz,pn	%0, 1b\n\t"
			 "	 nop"
			 : "=&r" (__tmp1), "=&r" (__tmp2), "=m" (*__mem)
			 : "r" (__mem), "r" (__val_extended), "m" (*__mem));
    return __tmp2;
  }
  
  void
  __attribute__ ((__unused__))
  __atomic_add(volatile _Atomic_word* __mem, int __val)
  {
    _Atomic_word __tmp1, __tmp2;
    _Atomic_word __val_extended = __val;
    
    __asm__ __volatile__("1:	ldx	[%3], %0\n\t"
			 "	add	%0, %4, %1\n\t"
			 "	casx	[%3], %0, %1\n\t"
			 "	sub	%0, %1, %0\n\t"
			 "	brnz,pn	%0, 1b\n\t"
			 "	 nop"
			 : "=&r" (__tmp1), "=&r" (__tmp2), "=m" (*__mem)
			 : "r" (__mem), "r" (__val_extended), "m" (*__mem));
  }
  
#else /* __arch32__ */

  template<int __inst>
    struct _Atomicity_lock
    {
      static unsigned char _S_atomicity_lock;
    };

  template<int __inst>
  unsigned char _Atomicity_lock<__inst>::_S_atomicity_lock = 0;
  
  template unsigned char _Atomicity_lock<0>::_S_atomicity_lock;
  
  _Atomic_word
  __attribute__ ((__unused__))
  __exchange_and_add(volatile _Atomic_word* __mem, int __val)
  {
    _Atomic_word __result, __tmp;
    
    __asm__ __volatile__("1:	ldstub	[%1], %0\n\t"
			 "	cmp	%0, 0\n\t"
			 "	bne	1b\n\t"
			 "	 nop"
			 : "=&r" (__tmp)
			 : "r" (&_Atomicity_lock<0>::_S_atomicity_lock)
			 : "memory");
    __result = *__mem;
    *__mem += __val;
    __asm__ __volatile__("stb	%%g0, [%0]"
			 : /* no outputs */
			 : "r" (&_Atomicity_lock<0>::_S_atomicity_lock)
			 : "memory");
    return __result;
  }
  
  void
  __attribute__ ((__unused__))
  __atomic_add(volatile _Atomic_word* __mem, int __val)
  {
    _Atomic_word __tmp;
    
    __asm__ __volatile__("1:	ldstub	[%1], %0\n\t"
			 "	cmp	%0, 0\n\t"
			 "	bne	1b\n\t"
			 "	 nop"
			 : "=&r" (__tmp)
			 : "r" (&_Atomicity_lock<0>::_S_atomicity_lock)
			 : "memory");
    *__mem += __val;
    __asm__ __volatile__("stb	%%g0, [%0]"
			 : /* no outputs */
			 : "r" (&_Atomicity_lock<0>::_S_atomicity_lock)
			 : "memory");
  }  
#endif /* __arch32__ */

_GLIBCXX_END_NAMESPACE
