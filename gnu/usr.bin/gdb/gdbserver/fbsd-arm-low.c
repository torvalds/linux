/* FreeBSD/ARM specific low level interface, for the remote server for GDB.
   Copyright 1995, 1996, 1998, 1999, 2000, 2001, 2002, 2003, 2004
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

#ifdef HAVE_SYS_REG_H
#include <sys/reg.h>
#endif

#include <sys/procfs.h>
#include <sys/ptrace.h>

#define arm_num_regs 26

static int arm_regmap[] = {
  0, 4, 8, 12, 16, 20, 24, 28,
  32, 36, 40, 44, 48, 52, 56, 60,
  -1, -1, -1, -1, -1, -1, -1, -1, -1,
  64
};

static int
arm_cannot_store_register (int regno)
{
  return (regno >= arm_num_regs);
}

static int
arm_cannot_fetch_register (int regno)
{
  return (regno >= arm_num_regs);
}

extern int debug_threads;

static CORE_ADDR
arm_get_pc ()
{
  unsigned long pc;
  collect_register_by_name ("pc", &pc);
  if (debug_threads)
    fprintf (stderr, "stop pc is %08lx\n", pc);
  return pc;
}

static void
arm_set_pc (CORE_ADDR pc)
{
  unsigned long newpc = pc;
  supply_register_by_name ("pc", &newpc);
}

/* Correct in either endianness.  We do not support Thumb yet.  */
static const unsigned long arm_breakpoint = 0xef9f0001;
#define arm_breakpoint_len 4

static int
arm_breakpoint_at (CORE_ADDR where)
{
  unsigned long insn;

  (*the_target->read_memory) (where, (char *) &insn, 4);
  if (insn == arm_breakpoint)
    return 1;

  /* If necessary, recognize more trap instructions here.  GDB only uses the
     one.  */
  return 0;
}

/* We only place breakpoints in empty marker functions, and thread locking
   is outside of the function.  So rather than importing software single-step,
   we can just run until exit.  */
static CORE_ADDR
arm_reinsert_addr ()
{
  unsigned long pc;
  collect_register_by_name ("lr", &pc);
  return pc;
}

static void
arm_fill_gregset (void *buf)
{
  int i;

  for (i = 0; i < arm_num_regs; i++)
	if (arm_regmap[i] != -1)
		collect_register (i, ((char *) buf) + arm_regmap[i]);

}

static void
arm_store_gregset (const void *buf)
{
  int i;

  for (i = 0; i < arm_num_regs; i++)
	if (arm_regmap[i] != -1)
		supply_register (i, ((char *) buf) + arm_regmap[i]);

}


struct regset_info target_regsets[] = {
    {PT_GETREGS, PT_SETREGS, sizeof (struct reg),
    GENERAL_REGS,
    arm_fill_gregset, arm_store_gregset },
    { 0, 0, -1, -1, NULL, NULL }
};

struct fbsd_target_ops the_low_target = {
  arm_num_regs,
  arm_regmap,
  arm_cannot_fetch_register,
  arm_cannot_store_register,
  arm_get_pc,
  arm_set_pc,
  (const char *) &arm_breakpoint,
  arm_breakpoint_len,
  arm_reinsert_addr,
  0,
  arm_breakpoint_at,
};
