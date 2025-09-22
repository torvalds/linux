// Specific definitions for GNU/Linux  -*- C++ -*-

// Copyright (C) 2000, 2001, 2002 Free Software Foundation, Inc.
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

// This keeps isanum, et al from being propagated as macros.
#define __NO_CTYPE 1

#include <features.h>

#if !defined (__GLIBC__) || (__GLIBC__ == 2 && __GLIBC_MINOR__+ 0 == 0)

// The types __off_t and __off64_t are not defined through <sys/types.h>
// as _G_config assumes.  For libc5 and glibc 2.0 instead use
// <gnu/types.h> and the old name for __off64_t.
#include <gnu/types.h>
typedef __loff_t __off64_t;

// These systems have declarations mismatching those in libio.h by
// omitting throw qualifiers.  Cleanest way out is to not provide
// throw-qualifiers at all.  Defining it as empty here will make libio.h
// not define it.
#undef __THROW
#define __THROW

// Tell Glibc not to try to provide its own inline versions of
// some math functions.  Those cause assembly-time clashes with
// our definitions.
#define __NO_MATH_INLINES

#endif 

#if defined __GLIBC__ && __GLIBC__ >= 2
// We must not see the optimized string functions GNU libc defines.
#define __NO_STRING_INLINES
#endif

#endif
