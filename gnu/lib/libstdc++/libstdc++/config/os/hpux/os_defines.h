// Specific definitions for HPUX  -*- C++ -*-

// Copyright (C) 2000, 2002 Free Software Foundation, Inc.
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

#ifndef _GLIBCPP_OS_DEFINES
#define _GLIBCPP_OS_DEFINES 1

// System-specific #define, typedefs, corrections, etc, go here.  This
// file will come before all others.

#define __off_t off_t
#define __off64_t off64_t
#define __ssize_t ssize_t

// Use macro form of ctype functions to ensure __SB_masks is defined.
#define _SB_CTYPE_MACROS 1

/* HP-UX, for reasons unknown choose to use a different name for
   the string to [unsigned] long long conversion routines.

   Furthermore, instead of having the prototypes in stdlib.h like
   everyone else, they put them into a non-standard header
   <inttypes.h>.  Ugh.

   <inttypes.h> defines a variety of things, some of which we 
   probably do not want.  So we don't want to include it here.

   Luckily we can just declare strtoll and strtoull with the
   __asm extension which effectively renames calls at the
   source level without namespace pollution.

   Also note that the compiler defines _INCLUDE_LONGLONG for C++
   unconditionally, which makes intmax_t and uintmax_t long long
   types.

   We also force _GLIBCPP_USE_LONG_LONG here so that we don't have
   to bastardize configure to deal with this sillyness.  */
namespace std 
{
#ifndef __LP64__
  __extension__ extern "C" long long strtoll (const char *, char **, int)
    __asm  ("__strtoll");
  __extension__ extern "C" unsigned long long strtoull (const char *, char **, int)
    __asm  ("__strtoull");
#else
  __extension__ extern "C" long long strtoll (const char *, char **, int)
    __asm  ("strtol");
  __extension__ extern "C" unsigned long long strtoull (const char *, char **, int)
    __asm  ("strtoul");
#endif
}

#define _GLIBCPP_USE_LONG_LONG 1

// HPUX on IA64 requires vtable to be 64 bit aligned even at 32 bit
// mode.  We need to pad the vtable structure to achieve this.  
#if !defined(_LP64) && defined (__ia64__)
#define _GLIBCPP_VTABLE_PADDING 8
typedef long int __padding_type;
#endif

// GCC on IA64 HP-UX uses the HP-UX system unwind library,
// it does not have the _Unwind_Resume_or_Rethrow entry point
// because that is not part of the standard IA64 Unwind ABI.
#if defined (__ia64__)
#define _LIBUNWIND_STD_ABI 1
#endif

/* We need explicit instantiation of the atomicity lock on HPPA if
   there is no weak support.  */
#if !defined(_GLIBCPP_SUPPORTS_WEAK) && defined (__hppa__)
#define _GLIBCPP_INST_ATOMICITY_LOCK 1
#endif

/* Don't use pragma weak in gthread headers.  HP-UX rejects programs
   with unsatisfied external references even if all of those references
   are weak; gthread relies on such unsatisfied references being resolved
   to null pointers when weak symbol support is on.  */
#define _GLIBCPP_GTHREAD_USE_WEAK 0

#endif
