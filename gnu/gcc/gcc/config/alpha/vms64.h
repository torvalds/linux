/* Output variables, constants and external declarations, for GNU compiler.
   Copyright (C) 2001 Free Software Foundation, Inc.
   Contributed by Douglas Rupp (rupp@gnat.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Defaults to BITS_PER_WORD, e.g. 64 which is what is wanted.
  This is incompatible with DEC C, but matches DEC Ada */
#undef LONG_TYPE_SIZE

/* Defaults to "long int" */
#undef SIZE_TYPE
#undef PTRDIFF_TYPE

#undef POINTERS_EXTEND_UNSIGNED
#undef POINTER_SIZE
#define POINTER_SIZE 64
