/* Sorting algorithms.
   Copyright (C) 2000, 2002 Free Software Foundation, Inc.
   Contributed by Mark Mitchell <mark@codesourcery.com>.

This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifndef SORT_H
#define SORT_H

#include <sys/types.h> /* For size_t */
#ifdef __STDC__
#include <stddef.h>
#endif	/* __STDC__ */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "ansidecl.h"

/* Sort an array of pointers.  */

extern void sort_pointers (size_t, void **, void **);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SORT_H */


   
   
