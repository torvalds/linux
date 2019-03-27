/* $FreeBSD$ */

/* Target-specific functions for ARM running under NetBSD.
   Copyright 2002, 2003 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "osabi.h"

#include "arch-utils.h"
#include "arm-tdep.h"
#include "solib-svr4.h"

/* Description of the longjmp buffer.  */
#define ARM_FBSD_JB_PC 24
#define ARM_FBSD_JB_ELEMENT_SIZE INT_REGISTER_SIZE

static void
arm_freebsd_init_abi_common (struct gdbarch_info info,
			    struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  tdep->lowest_pc = 0x8000;

  tdep->jb_pc = ARM_FBSD_JB_PC;
  tdep->jb_elt_size = ARM_FBSD_JB_ELEMENT_SIZE;
}
  
static void
arm_freebsd_elf_init_abi (struct gdbarch_info info, 
			 struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  arm_freebsd_init_abi_common (info, gdbarch);

  /* FreeBSD ELF uses SVR4-style shared libraries.  */
  set_gdbarch_in_solib_call_trampoline
    (gdbarch, generic_in_solib_call_trampoline);
  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_ilp32_fetch_link_map_offsets);

  tdep->fp_model = ARM_FLOAT_SOFT_VFP;
}

void
_initialize_armfbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_arm, 0, GDB_OSABI_FREEBSD_ELF,
                          arm_freebsd_elf_init_abi);
}
