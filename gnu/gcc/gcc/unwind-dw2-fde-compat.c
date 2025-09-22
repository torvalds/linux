/* Backward compatibility unwind routines.
   Copyright (C) 2004, 2005
   Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combined
   executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#if defined (USE_GAS_SYMVER) && defined (USE_LIBUNWIND_EXCEPTIONS)
#include "tconfig.h"
#include "tsystem.h"
#include "unwind.h"
#include "unwind-dw2-fde.h"
#include "unwind-compat.h"

extern const fde * __libunwind__Unwind_Find_FDE
  (void *, struct dwarf_eh_bases *);

const fde *
_Unwind_Find_FDE (void *pc, struct dwarf_eh_bases *bases)
{
  __libunwind__Unwind_Find_FDE (pc, bases);
}

symver (_Unwind_Find_FDE, GCC_3.0);
#endif
