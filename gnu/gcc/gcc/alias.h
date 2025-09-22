/* Exported functions from alias.c
   Copyright (C) 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_ALIAS_H
#define GCC_ALIAS_H

extern HOST_WIDE_INT new_alias_set (void);
extern HOST_WIDE_INT get_varargs_alias_set (void);
extern HOST_WIDE_INT get_frame_alias_set (void);
extern bool component_uses_parent_alias_set (tree);

/* This alias set can be used to force a memory to conflict with all
   other memories, creating a barrier across which no memory reference
   can move.  Note that there are other legacy ways to create such
   memory barriers, including an address of SCRATCH.  */
#define ALIAS_SET_MEMORY_BARRIER	((HOST_WIDE_INT) -1)

#endif /* GCC_ALIAS_H */
