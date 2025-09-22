/* Null garbage collection for the GNU compiler.
   Copyright (C) 1998, 1999, 2000, 2003, 2004, 2005
   Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This version is used by the gen* programs and certain language-specific
   targets (such as java), where we don't really need GC at all.
   This prevents problems with pulling in all the tree stuff.  */

#ifdef GENERATOR_FILE
#include "bconfig.h"
#else
#include "config.h"
#endif

#include "system.h"
#include "coretypes.h"
#include "ggc.h"

void *
ggc_alloc_typed_stat (enum gt_types_enum ARG_UNUSED (gte), size_t size
		      MEM_STAT_DECL)
{
  return xmalloc (size);
}

void *
ggc_alloc_stat (size_t size MEM_STAT_DECL)
{
  return xmalloc (size);
}

void *
ggc_alloc_cleared_stat (size_t size MEM_STAT_DECL)
{
  return xcalloc (size, 1);
}

void *
ggc_realloc_stat (void *x, size_t size MEM_STAT_DECL)
{
  return xrealloc (x, size);
}

void
ggc_free (void *p)
{
  free (p);
}
