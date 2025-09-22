// Prototypes for GLIBC thread locale __-prefixed functions -*- C++ -*-

// Copyright (C) 2002 Free Software Foundation, Inc.
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

// Written by Jakub Jelinek <jakub@redhat.com>

#include <clocale>

#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
                                                  
extern "C" __typeof(iswctype_l) __iswctype_l;
extern "C" __typeof(nl_langinfo_l) __nl_langinfo_l;
extern "C" __typeof(strcoll_l) __strcoll_l;
extern "C" __typeof(strftime_l) __strftime_l;
extern "C" __typeof(strtod_l) __strtod_l;
extern "C" __typeof(strtof_l) __strtof_l;
extern "C" __typeof(strtold_l) __strtold_l;
extern "C" __typeof(strtol_l) __strtol_l;
extern "C" __typeof(strtoll_l) __strtoll_l;
extern "C" __typeof(strtoul_l) __strtoul_l;
extern "C" __typeof(strtoull_l) __strtoull_l;
extern "C" __typeof(strxfrm_l) __strxfrm_l;
extern "C" __typeof(towlower_l) __towlower_l;
extern "C" __typeof(towupper_l) __towupper_l;
extern "C" __typeof(wcscoll_l) __wcscoll_l;
extern "C" __typeof(wcsftime_l) __wcsftime_l;
extern "C" __typeof(wcsxfrm_l) __wcsxfrm_l;
extern "C" __typeof(wctype_l) __wctype_l;
extern "C" __typeof(newlocale) __newlocale;
extern "C" __typeof(freelocale) __freelocale;
extern "C" __typeof(duplocale) __duplocale;
extern "C" __typeof(uselocale) __uselocale;

#endif // GLIBC 2.3 and later
