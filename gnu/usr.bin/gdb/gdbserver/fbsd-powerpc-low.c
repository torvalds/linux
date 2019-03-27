/* FreeBSD/PowerPC specific low level interface, for the remote server for
   GDB.
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

#include <sys/procfs.h>
#include <sys/ptrace.h>

#define ppc_num_regs 71

/* Currently, don't check/send MQ.  */
static int ppc_regmap[] =
 { 0, 4, 8, 12, 16, 20, 24, 28,
   32, 36, 40, 44, 48, 52, 56, 60,
   64, 68, 72, 76, 80, 84, 88, 92,
   96, 100, 104, 108, 112, 116, 120, 124,
#if 0
  /*
   * XXX on FreeBSD the gdbserver for PowerPC was only tested with FPU-less
   * cores i.e. e500. Let's leave the original FPR references around in case
   * someone picks up and brings support for AIM-like FPU machines.
   */
  PT_FPR0*4,     PT_FPR0*4 + 8, PT_FPR0*4+16,  PT_FPR0*4+24,
  PT_FPR0*4+32,  PT_FPR0*4+40,  PT_FPR0*4+48,  PT_FPR0*4+56,
  PT_FPR0*4+64,  PT_FPR0*4+72,  PT_FPR0*4+80,  PT_FPR0*4+88,
  PT_FPR0*4+96,  PT_FPR0*4+104,  PT_FPR0*4+112,  PT_FPR0*4+120,
  PT_FPR0*4+128, PT_FPR0*4+136,  PT_FPR0*4+144,  PT_FPR0*4+152,
  PT_FPR0*4+160,  PT_FPR0*4+168,  PT_FPR0*4+176,  PT_FPR0*4+184,
  PT_FPR0*4+192,  PT_FPR0*4+200,  PT_FPR0*4+208,  PT_FPR0*4+216,
  PT_FPR0*4+224,  PT_FPR0*4+232,  PT_FPR0*4+240,  PT_FPR0*4+248,
#endif
   -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1,
   144, -1, 132, 128, 140, 136, -1
 };

static int
ppc_cannot_store_register (int regno)
{
  /* Some kernels do not allow us to store fpscr.  */
  if (regno == find_regno ("fpscr"))
    return 2;

  return 0;
}

static int
ppc_cannot_fetch_register (int regno)
{
  return 0;
}

static CORE_ADDR
ppc_get_pc (void)
{
  unsigned long pc;

  collect_register_by_name ("pc", &pc);
  return (CORE_ADDR) pc;
}

static void
ppc_set_pc (CORE_ADDR pc)
{
  unsigned long newpc = pc;

  supply_register_by_name ("pc", &newpc);
}

/* Correct in either endianness.  Note that this file is
   for PowerPC only, not PowerPC64.
   This instruction is "twge r2, r2", which GDB uses as a software
   breakpoint.  */
static const unsigned long ppc_breakpoint = 0x7d821008;
#define ppc_breakpoint_len 4

static int
ppc_breakpoint_at (CORE_ADDR where)
{
  unsigned long insn;

  (*the_target->read_memory) (where, (char *) &insn, 4);
  if (insn == ppc_breakpoint)
    return 1;
  /* If necessary, recognize more trap instructions here.  GDB only uses the
     one.  */
  return 0;
}

static void
ppc_fill_gregset (void *buf)
{
  int i;

  for (i = 0; i < ppc_num_regs; i++)
	if (ppc_regmap[i] != -1)
		collect_register (i, ((char *) buf) + ppc_regmap[i]);

}

static void
ppc_store_gregset (const void *buf)
{
  int i;

  for (i = 0; i < ppc_num_regs; i++)
	if (ppc_regmap[i] != -1)
		supply_register (i, ((char *) buf) + ppc_regmap[i]);

}

struct regset_info target_regsets[] = {
  { PT_GETREGS, PT_SETREGS, sizeof (struct reg),
    GENERAL_REGS,
    ppc_fill_gregset, ppc_store_gregset },
  { 0, 0, -1, -1, NULL, NULL }
};

struct fbsd_target_ops the_low_target = {
  ppc_num_regs,
  ppc_regmap,
  ppc_cannot_fetch_register,
  ppc_cannot_store_register,
  ppc_get_pc,
  ppc_set_pc,
  (const char *) &ppc_breakpoint,
  ppc_breakpoint_len,
  NULL,
  0,
  ppc_breakpoint_at,
};
