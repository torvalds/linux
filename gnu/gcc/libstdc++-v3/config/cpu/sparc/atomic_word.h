// Low-level type for atomic operations -*- C++ -*-

// Copyright (C) 2004 Free Software Foundation, Inc.
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

#ifndef _GLIBCXX_ATOMIC_WORD_H
#define _GLIBCXX_ATOMIC_WORD_H	1

#ifdef __arch64__
  typedef long _Atomic_word;
#else
  typedef int _Atomic_word;
#endif

#if defined(__sparc_v9__)
// These are necessary under the V9 RMO model, though it is almost never
// used in userspace.
#define _GLIBCXX_READ_MEM_BARRIER \
  __asm __volatile ("membar #LoadLoad":::"memory")
#define _GLIBCXX_WRITE_MEM_BARRIER \
  __asm __volatile ("membar #StoreStore":::"memory")

#elif defined(__sparc_v8__)
// This is necessary under the PSO model.
#define _GLIBCXX_WRITE_MEM_BARRIER __asm __volatile ("stbar":::"memory")

#endif

#endif 
