/* GNU/Linux/i386 specific low level interface, for the remote server for GDB.
   Copyright 1995, 1996, 1998, 1999, 2000, 2001, 2002
   Free Software Foundation, Inc.

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "server.h"
#include "fbsd-low.h"
#include "i387-fp.h"

#ifdef HAVE_SYS_REG_H
#include <sys/reg.h>
#else
#include <machine/reg.h>
#endif

/* This module only supports access to the general purpose registers.  */

#define i386_num_regs 16

/* This stuff comes from i386-fbsd-nat.c.  */

/* Mapping between the general-purpose registers in `struct user'
   format and GDB's register array layout.  */
static int i386_regmap[] = 
{
  tEAX * 4, tECX * 4, tEDX * 4, tEBX * 4,
  tESP * 4, tEBP * 4, tESI * 4, tEDI * 4,
  tEIP * 4, tEFLAGS * 4, tCS * 4, tSS * 4,
  tDS * 4, tES * 4, tFS * 4, tGS * 4
};

static int
i386_cannot_store_register (int regno)
{
  return (regno >= i386_num_regs);
}

static int
i386_cannot_fetch_register (int regno)
{
  return (regno >= i386_num_regs);
}


#include <sys/procfs.h>
#include <sys/ptrace.h>

static void
i386_fill_gregset (void *buf)
{
  int i;

  for (i = 0; i < i386_num_regs; i++)
    collect_register (i, ((char *) buf) + i386_regmap[i]);

}

static void
i386_store_gregset (const void *buf)
{
  int i;

  for (i = 0; i < i386_num_regs; i++)
    supply_register (i, ((char *) buf) + i386_regmap[i]);

}

static void
i386_fill_fpregset (void *buf)
{
  i387_cache_to_fsave (buf);
}

static void
i386_store_fpregset (const void *buf)
{
  i387_fsave_to_cache (buf);
}

static void
i386_fill_fpxregset (void *buf)
{
  i387_cache_to_fxsave (buf);
}

static void
i386_store_fpxregset (const void *buf)
{
  i387_fxsave_to_cache (buf);
}


struct regset_info target_regsets[] = {
  { PT_GETREGS, PT_SETREGS, sizeof (struct reg),
    GENERAL_REGS,
    i386_fill_gregset, i386_store_gregset },
#ifdef HAVE_PTRACE_GETFPXREGS
  { PTRACE_GETFPXREGS, PTRACE_SETFPXREGS, sizeof (elf_fpxregset_t),
    EXTENDED_REGS,
    i386_fill_fpxregset, i386_store_fpxregset },
#endif
  { PT_GETFPREGS, PT_SETFPREGS, sizeof (struct fpreg),
    FP_REGS,
    i386_fill_fpregset, i386_store_fpregset },
  { 0, 0, -1, -1, NULL, NULL }
};

static const char i386_breakpoint[] = { 0xCC };
#define i386_breakpoint_len 1

extern int debug_threads;

static CORE_ADDR
i386_get_pc ()
{
  unsigned long pc;

  collect_register_by_name ("eip", &pc);

  if (debug_threads)
    fprintf (stderr, "stop pc (before any decrement) is %08lx\n", pc);
  return pc;
}

static void
i386_set_pc (CORE_ADDR newpc)
{
  if (debug_threads)
    fprintf (stderr, "set pc to %08lx\n", (long) newpc);
  supply_register_by_name ("eip", &newpc);
}

static int
i386_breakpoint_at (CORE_ADDR pc)
{
  unsigned char c;

  read_inferior_memory (pc, &c, 1);
  if (c == 0xCC)
    return 1;

  return 0;
}

struct fbsd_target_ops the_low_target = {
  i386_num_regs,
  i386_regmap,
  i386_cannot_fetch_register,
  i386_cannot_store_register,
  i386_get_pc,
  i386_set_pc,
  i386_breakpoint,
  i386_breakpoint_len,
  NULL,
  1,
  i386_breakpoint_at,
};
