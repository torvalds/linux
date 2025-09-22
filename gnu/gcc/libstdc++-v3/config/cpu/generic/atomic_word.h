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

/** @file atomic_word.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _GLIBCXX_ATOMIC_WORD_H
#define _GLIBCXX_ATOMIC_WORD_H	1

typedef int _Atomic_word;

// Define these two macros using the appropriate memory barrier for the target.
// The commented out versions below are the defaults.
// See ia64/atomic_word.h for an alternative approach.

// This one prevents loads from being hoisted across the barrier;
// in other words, this is a Load-Load acquire barrier.
// This is necessary iff TARGET_RELAXED_ORDERING is defined in tm.h.  
// #define _GLIBCXX_READ_MEM_BARRIER __asm __volatile ("":::"memory")

// This one prevents stores from being sunk across the barrier; in other
// words, a Store-Store release barrier.
// #define _GLIBCXX_WRITE_MEM_BARRIER __asm __volatile ("":::"memory")

#endif 
