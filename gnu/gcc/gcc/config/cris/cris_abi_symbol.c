/* Define symbol to recognize CRIS ABI version 2, for a.out use.
   Contributed by Axis Communications.
   Written by Hans-Peter Nilsson <hp@axis.se>, c:a 1992.

   Copyright (C) 2000, 2001, 2003 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.

   As a special exception, if you link this library with files, some of
   which are compiled with GCC, this library does not by itself cause
   the resulting object or executable to be covered by the GNU General
   Public License.
   This exception does not however invalidate any other reasons why
   the executable file or object might be covered by the GNU General
   Public License.  */

#include "tconfig.h"
#include "tm.h"

#ifdef __AOUT__

/* ELF support was not released before the ABI was changed, so we
   restrict this awkwardness to a.out.  This symbol is for gdb to
   recognize, so it can debug both old and new programs successfully.  */
__asm__ (".global " CRIS_ABI_VERSION_SYMBOL_STRING);
__asm__ (".set " CRIS_ABI_VERSION_SYMBOL_STRING ",0");

#else  /* not __AOUT__ */

/* The file must not be empty (declaration/definition-wise) according to
   ISO, IIRC. */
extern int _Dummy;

#endif /* not __AOUT__ */
