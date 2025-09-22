/* Subroutine for function pointer canonicalization on PA-RISC with ELF32.
   Copyright 2002, 2003, 2004, 2007 Free Software Foundation, Inc.
   Contributed by John David Anglin (dave.anglin@nrc.ca).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* WARNING: The code is this function depends on internal and undocumented
   details of the GNU linker and dynamic loader as implemented for parisc
   linux.  */

/* This MUST match the defines sysdeps/hppa/dl-machine.h and
   bfd/elf32-hppa.c.  */
#define GOT_FROM_PLT_STUB (4*4)

/* List of byte offsets in _dl_runtime_resolve to search for "bl" branches.
   The first "bl" branch instruction found MUST be a call to fixup.  See
   the define for TRAMPOLINE_TEMPLATE in sysdeps/hppa/dl-machine.h.  If
   the trampoline template is changed, the list must be appropriately
   updated.  The offset of -4 allows for a magic branch at the start of
   the template should it be necessary to change the current branch
   position.  */
#define NOFFSETS 2
static int fixup_branch_offset[NOFFSETS] = { 32, -4 };

#define GET_FIELD(X, FROM, TO) \
  ((X) >> (31 - (TO)) & ((1 << ((TO) - (FROM) + 1)) - 1))
#define SIGN_EXTEND(VAL,BITS) \
  ((int) ((VAL) >> ((BITS) - 1) ? (-1 << (BITS)) | (VAL) : (VAL)))

struct link_map;
typedef int (*fptr_t) (void);
typedef int (*fixup_t) (struct link_map *, unsigned int);
extern unsigned int _GLOBAL_OFFSET_TABLE_;

/* __canonicalize_funcptr_for_compare must be hidden so that it is not
   placed in the dynamic symbol table.  Like millicode functions, it
   must be linked into all binaries in order access the got table of 
   that binary.  However, we don't use the millicode calling convention
   and the routine must be a normal function so that it can be compiled
   as pic code.  */
unsigned int __canonicalize_funcptr_for_compare (fptr_t)
      __attribute__ ((visibility ("hidden")));

unsigned int
__canonicalize_funcptr_for_compare (fptr_t fptr)
{
  static unsigned int fixup_plabel[2];
  static fixup_t fixup;
  unsigned int *plabel, *got;

  /* -1 and page 0 are special.  -1 is used in crtend to mark the end of
     a list of function pointers.  Also return immediately if the plabel
     bit is not set in the function pointer.  In this case, the function
     pointer points directly to the function.  */
  if ((int) fptr == -1 || (unsigned int) fptr < 4096 || !((int) fptr & 2))
    return (unsigned int) fptr;

  /* The function pointer points to a function descriptor (plabel).  If
     the plabel hasn't been resolved, the first word of the plabel points
     to the entry of the PLT stub just before the global offset table.
     The second word in the plabel contains the relocation offset for the
     function.  */
  plabel = (unsigned int *) ((unsigned int) fptr & ~3);
  got = (unsigned int *) (plabel[0] + GOT_FROM_PLT_STUB);

  /* Return the address of the function if the plabel has been resolved.  */
  if (got !=  &_GLOBAL_OFFSET_TABLE_)
    return plabel[0];

  /* Initialize our plabel for calling fixup if we haven't done so already.
     This code needs to be thread safe but we don't have to be too careful
     as the result is invariant.  */
  if (!fixup)
    {
      /* On OpenBSD, we have a magic branch to fixup just before the
         trampoline template.  The fixup function does the actual
         runtime resolution of function descriptors.  */

      /* Build a plabel for an indirect call to fixup.  */
      fixup_plabel[0] = got[-2] - 4;		  /* address of fixup */
      fixup_plabel[1] = got[-1];		  /* ltp for fixup */
      fixup = (fixup_t) ((int) fixup_plabel | 3);
    }

  /* Call fixup to resolve the function address.  got[1] contains the
     link_map pointer and plabel[1] the relocation offset.  */
  fixup ((struct link_map *) got[1], plabel[1]);

  return plabel[0];
}
