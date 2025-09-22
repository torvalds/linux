/* IPA handling of references.
   Copyright (C) 2004-2005 Free Software Foundation, Inc.
   Contributed by Kenneth Zadeck <zadeck@naturalbridge.com>

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

#ifndef GCC_IPA_REFERENCE_H
#define GCC_IPA_REFERENCE_H
#include "bitmap.h"
#include "tree.h"

/* The static variables defined within the compilation unit that are
   loaded or stored directly by function that owns this structure.  */ 

struct ipa_reference_local_vars_info_d 
{
  bitmap statics_read;
  bitmap statics_written;

  /* Set when this function calls another function external to the
     compilation unit or if the function has a asm clobber of memory.
     In general, such calls are modeled as reading and writing all
     variables (both bits on) but sometime there are attributes on the
     called function so we can do better.  */
  bool calls_read_all;
  bool calls_write_all;
};

struct ipa_reference_global_vars_info_d
{
  bitmap statics_read;
  bitmap statics_written;
  bitmap statics_not_read;
  bitmap statics_not_written;
};

/* Statics that are read and written by some set of functions. The
   local ones are based on the loads and stores local to the function.
   The global ones are based on the local info as well as the
   transitive closure of the functions that are called.  The
   structures are separated to allow the global structures to be
   shared between several functions since every function within a
   strongly connected component will have the same information.  This
   sharing saves both time and space in the computation of the vectors
   as well as their translation from decl_uid form to ann_uid
   form.  */ 

typedef struct ipa_reference_local_vars_info_d *ipa_reference_local_vars_info_t;
typedef struct ipa_reference_global_vars_info_d *ipa_reference_global_vars_info_t;

struct ipa_reference_vars_info_d 
{
  ipa_reference_local_vars_info_t local;
  ipa_reference_global_vars_info_t global;
};

typedef struct ipa_reference_vars_info_d *ipa_reference_vars_info_t;

/* In ipa-reference.c  */
bitmap ipa_reference_get_read_local (tree fn);
bitmap ipa_reference_get_written_local (tree fn);
bitmap ipa_reference_get_read_global (tree fn);
bitmap ipa_reference_get_written_global (tree fn);
bitmap ipa_reference_get_not_read_global (tree fn);
bitmap ipa_reference_get_not_written_global (tree fn);

#endif  /* GCC_IPA_REFERENCE_H  */

