// Control various target specific ABI tweaks.  ARM version.

// Copyright (C) 2004, 2006 Free Software Foundation, Inc.
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

/** @file cxxabi_tweaks.h
 *  The header provides an CPU-variable interface to the C++ ABI.
 */

#ifndef _CXXABI_TWEAKS_H
#define _CXXABI_TWEAKS_H 1

#ifdef __cplusplus
namespace __cxxabiv1
{
  extern "C" 
  {
#endif

#ifdef __ARM_EABI__
  // The ARM EABI uses the least significant bit of a 32-bit
  // guard variable.  */
#define _GLIBCXX_GUARD_TEST(x) ((*(x) & 1) != 0)
#define _GLIBCXX_GUARD_SET(x) *(x) = 1
  typedef int __guard;

  // We also want the element size in array cookies.
#define _GLIBCXX_ELTSIZE_IN_COOKIE 1
  
  // __cxa_vec_ctor should return a pointer to the array.
  typedef void * __cxa_vec_ctor_return_type;
#define _GLIBCXX_CXA_VEC_CTOR_RETURN(x) return x
  // Constructors and destructors return the "this" pointer.
  typedef void * __cxa_cdtor_return_type;

#else // __ARM_EABI__

  // The generic ABI uses the first byte of a 64-bit guard variable.
#define _GLIBCXX_GUARD_TEST(x) (*(char *) (x) != 0)
#define _GLIBCXX_GUARD_SET(x) *(char *) (x) = 1
  __extension__ typedef int __guard __attribute__((mode (__DI__)));

  // __cxa_vec_ctor has void return type.
  typedef void __cxa_vec_ctor_return_type;
#define _GLIBCXX_CXA_VEC_CTOR_RETURN(x) return
  // Constructors and destructors do not return a value.
  typedef void __cxa_cdtor_return_type;

#endif //!__ARM_EABI__

#ifdef __cplusplus
  }
} // namespace __cxxabiv1
#endif

#endif 
