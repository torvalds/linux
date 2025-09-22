/* Header for a pass computing data for optimizing stdarg functions.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Jakub Jelinek <jakub@redhat.com>

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

#ifndef GCC_TREE_STDARG_H
#define GCC_TREE_STDARG_H 1

struct stdarg_info
{
  bitmap va_list_vars, va_list_escape_vars;
  basic_block bb;
  int compute_sizes, va_start_count;
  bool va_list_escapes;
  int *offsets;
  /* These 2 fields are only meaningful if va_start_count == 1.  */
  basic_block va_start_bb;
  tree va_start_ap;
};

#endif
