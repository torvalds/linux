// Low-level functions for atomic operations: x86, x >= 3 version  -*- C++ -*-

// Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
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

  template<int __inst>
    struct _Atomicity_lock
    {
      static volatile _Atomic_word _S_atomicity_lock;
    };

  template<int __inst>
  volatile _Atomic_word _Atomicity_lock<__inst>::_S_atomicity_lock = 0;

  template volatile _Atomic_word _Atomicity_lock<0>::_S_atomicity_lock;
  
  _Atomic_word 
  __attribute__ ((__unused__))
  __exchange_and_add(volatile _Atomic_word* __mem, int __val)
  {
    register _Atomic_word __result, __tmp = 1;
    
    // Obtain the atomic exchange/add spin lock.
    do 
      {
	__asm__ __volatile__ ("xchg{l} {%0,%1|%1,%0}"
			      : "=m" (_Atomicity_lock<0>::_S_atomicity_lock),
			      "+r" (__tmp)
			      : "m" (_Atomicity_lock<0>::_S_atomicity_lock));
      } 
    while (__tmp);
    
    __result = *__mem;
    *__mem += __val;
    
    // Release spin lock.
    _Atomicity_lock<0>::_S_atomicity_lock = 0;
    
    return __result;
  }
  
  void
  __attribute__ ((__unused__))
  __atomic_add(volatile _Atomic_word* __mem, int __val)
  { __exchange_and_add(__mem, __val); }

_GLIBCXX_END_NAMESPACE
