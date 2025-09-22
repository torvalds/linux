/* Provide prototypes for functions exported from prefix.c.
   Copyright (C) 1999, 2003 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU Library General Public License as published by
the Free Software Foundation; either version 2 of the License, or (at
your option) any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */


#ifndef GCC_PREFIX_H
#define GCC_PREFIX_H

/* Update PATH using KEY if PATH starts with PREFIX.  The returned
   string is always malloc-ed, and the caller is responsible for
   freeing it.  */
extern char *update_path (const char *path, const char *key);
extern void set_std_prefix (const char *, int);

#endif /* ! GCC_PREFIX_H */
