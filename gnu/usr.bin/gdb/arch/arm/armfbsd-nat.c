/* $FreeBSD$ */

 /* Native-dependent code for BSD Unix running on ARM's, for GDB.
   Copyright 1988, 1989, 1991, 1992, 1994, 1996, 1999, 2002
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

#include "defs.h"

#ifndef FETCH_INFERIOR_REGISTERS
#ifndef CROSS_DEBUGGER
#error Not FETCH_INFERIOR_REGISTERS 
#endif
#endif /* !FETCH_INFERIOR_REGISTERS */

#include "arm-tdep.h"

#include <sys/types.h>
#ifndef CROSS_DEBUGGER
#include <sys/ptrace.h>
#include <machine/reg.h>
#include <machine/frame.h>
#endif
#include "inferior.h"
#include "regcache.h"
#include "gdbcore.h"

extern int arm_apcs_32;

#ifdef CROSS_DEBUGGER
struct reg {
	unsigned int r[13];
	unsigned int r_sp;
	unsigned int r_lr;
	unsigned int r_pc;
	unsigned int r_cpsr;
};

typedef struct fp_extended_precision {
	u_int32_t fp_exponent;
	u_int32_t fp_mantissa_hi;
	u_int32_t fp_mantissa_lo;
} fp_extended_precision_t;

typedef struct fp_extended_precision fp_reg_t;

struct fpreg {
	unsigned int fpr_fpsr;
	fp_reg_t fpr[8];
};
#endif

void
supply_gregset (struct reg *gregset)
{
  int regno;
  CORE_ADDR r_pc;

  /* Integer registers.  */
  for (regno = ARM_A1_REGNUM; regno < ARM_SP_REGNUM; regno++)
    supply_register (regno, (char *) &gregset->r[regno]);

  supply_register (ARM_SP_REGNUM, (char *) &gregset->r_sp);
  supply_register (ARM_LR_REGNUM, (char *) &gregset->r_lr);
  supply_register (ARM_PC_REGNUM, (char *) &gregset->r_pc);

  if (arm_apcs_32)
    supply_register (ARM_PS_REGNUM, (char *) &gregset->r_cpsr);
  else
    supply_register (ARM_PS_REGNUM, (char *) &gregset->r_pc);
}

/* Fill register REGNO (if it is a general-purpose register) in
   *GREGSETPS with the value in GDB's register array.  If REGNO is -1,
   do this for all registers.  */

void
fill_gregset (struct reg *gregset, int regno)
{
  int i;

  for (i = ARM_A1_REGNUM; i < ARM_SP_REGNUM; i++)
    if ((regno == -1 || regno == i))
      regcache_collect (i, &gregset->r[i]);
  if (regno == -1 || regno == ARM_SP_REGNUM)
      regcache_collect (ARM_SP_REGNUM, &gregset->r_sp);
  if (regno == -1 || regno == ARM_LR_REGNUM)
      regcache_collect (ARM_LR_REGNUM, &gregset->r_lr);
  if (regno == -1 || regno == ARM_PC_REGNUM)
      regcache_collect (ARM_PC_REGNUM, &gregset->r_pc);
  if (regno == -1 || regno == ARM_PS_REGNUM)
      regcache_collect (ARM_PS_REGNUM, &gregset->r_cpsr);
}

void
supply_fpregset (struct fpreg *fparegset)
{
  int regno;

  for (regno = ARM_F0_REGNUM; regno <= ARM_F7_REGNUM; regno++)
    supply_register
      (regno, (char *) &fparegset->fpr[regno - ARM_F0_REGNUM]);

  supply_register (ARM_FPS_REGNUM, (char *) &fparegset->fpr_fpsr);
}

void
fill_fpregset (struct fpreg *fparegset, int regno)
{
  int i;

  for (i = ARM_F0_REGNUM; i <= ARM_F7_REGNUM; i++)
    if (regno == -1 || regno == i)
      regcache_raw_supply(current_regcache, i,
	  &fparegset->fpr[i - ARM_F0_REGNUM]);
  if (regno == -1 || regno == ARM_FPS_REGNUM)
    regcache_raw_supply(current_regcache, ARM_FPS_REGNUM, 
	&fparegset->fpr_fpsr);
}

static void
fetch_register (int regno)
{
  struct reg inferior_registers;
#ifndef CROSS_DEBUGGER
  int ret;

  ret = ptrace (PT_GETREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_registers, 0);

  if (ret < 0)
    {
      warning ("unable to fetch general register");
      return;
    }
#endif

  switch (regno)
    {
    case ARM_SP_REGNUM:
      supply_register (ARM_SP_REGNUM, (char *) &inferior_registers.r_sp);
      break;

    case ARM_LR_REGNUM:
      supply_register (ARM_LR_REGNUM, (char *) &inferior_registers.r_lr);
      break;

    case ARM_PC_REGNUM:
      /* This is ok: we're running native... */
      inferior_registers.r_pc = ADDR_BITS_REMOVE (inferior_registers.r_pc);
      supply_register (ARM_PC_REGNUM, (char *) &inferior_registers.r_pc);
      break;

    case ARM_PS_REGNUM:
      if (arm_apcs_32)
	supply_register (ARM_PS_REGNUM, (char *) &inferior_registers.r_cpsr);
      else
	supply_register (ARM_PS_REGNUM, (char *) &inferior_registers.r_pc);
      break;

    default:
      supply_register (regno, (char *) &inferior_registers.r[regno]);
      break;
    }
}

static void
fetch_regs (void)
{
  struct reg inferior_registers;
#ifndef CROSS_DEBUGGER
  int ret;
#endif
  int regno;

#ifndef CROSS_DEBUGGER
  ret = ptrace (PT_GETREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_registers, 0);

  if (ret < 0)
    {
      warning ("unable to fetch general registers");
      return;
    }
#endif

  supply_gregset (&inferior_registers);
}

static void
fetch_fp_register (int regno)
{
  struct fpreg inferior_fp_registers;
#ifndef CROSS_DEBUGGER
  int ret;

  ret = ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_fp_registers, 0);

  if (ret < 0)
    {
      warning ("unable to fetch floating-point register");
      return;
    }
#endif

  switch (regno)
    {
    case ARM_FPS_REGNUM:
      supply_register (ARM_FPS_REGNUM,
		       (char *) &inferior_fp_registers.fpr_fpsr);
      break;

    default:
      supply_register
	(regno, (char *) &inferior_fp_registers.fpr[regno - ARM_F0_REGNUM]);
      break;
    }
}

static void
fetch_fp_regs (void)
{
  struct fpreg inferior_fp_registers;
#ifndef CROSS_DEBUGGER
  int ret;
#endif
  int regno;

#ifndef CROSS_DEBUGGER
  ret = ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_fp_registers, 0);

  if (ret < 0)
    {
      warning ("unable to fetch general registers");
      return;
    }
#endif

  supply_fpregset (&inferior_fp_registers);
}

void
fetch_inferior_registers (int regno)
{
  if (regno >= 0)
    {
      if (regno < ARM_F0_REGNUM || regno > ARM_FPS_REGNUM)
	fetch_register (regno);
      else
	fetch_fp_register (regno);
    }
  else
    {
      fetch_regs ();
      fetch_fp_regs ();
    }
}


static void
store_register (int regno)
{
  struct reg inferior_registers;
#ifndef CROSS_DEBUGGER
  int ret;

  ret = ptrace (PT_GETREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_registers, 0);

  if (ret < 0)
    {
      warning ("unable to fetch general registers");
      return;
    }
#endif

  switch (regno)
    {
    case ARM_SP_REGNUM:
      regcache_collect (ARM_SP_REGNUM, (char *) &inferior_registers.r_sp);
      break;

    case ARM_LR_REGNUM:
      regcache_collect (ARM_LR_REGNUM, (char *) &inferior_registers.r_lr);
      break;

    case ARM_PC_REGNUM:
      if (arm_apcs_32)
	regcache_collect (ARM_PC_REGNUM, (char *) &inferior_registers.r_pc);
      else
	{
	  unsigned pc_val;

	  regcache_collect (ARM_PC_REGNUM, (char *) &pc_val);
	  
	  pc_val = ADDR_BITS_REMOVE (pc_val);
	  inferior_registers.r_pc
	    ^= ADDR_BITS_REMOVE (inferior_registers.r_pc);
	  inferior_registers.r_pc |= pc_val;
	}
      break;

    case ARM_PS_REGNUM:
      if (arm_apcs_32)
	regcache_collect (ARM_PS_REGNUM, (char *) &inferior_registers.r_cpsr);
      else
	{
	  unsigned psr_val;

	  regcache_collect (ARM_PS_REGNUM, (char *) &psr_val);

	  psr_val ^= ADDR_BITS_REMOVE (psr_val);
	  inferior_registers.r_pc = ADDR_BITS_REMOVE (inferior_registers.r_pc);
	  inferior_registers.r_pc |= psr_val;
	}
      break;

    default:
      regcache_collect (regno, (char *) &inferior_registers.r[regno]);
      break;
    }

#ifndef CROSS_DEBUGGER
  ret = ptrace (PT_SETREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_registers, 0);

  if (ret < 0)
    warning ("unable to write register %d to inferior", regno);
#endif
}

static void
store_regs (void)
{
  struct reg inferior_registers;
  int ret;
  int regno;


  for (regno = ARM_A1_REGNUM; regno < ARM_SP_REGNUM; regno++)
    regcache_collect (regno, (char *) &inferior_registers.r[regno]);

  regcache_collect (ARM_SP_REGNUM, (char *) &inferior_registers.r_sp);
  regcache_collect (ARM_LR_REGNUM, (char *) &inferior_registers.r_lr);

  if (arm_apcs_32)
    {
      regcache_collect (ARM_PC_REGNUM, (char *) &inferior_registers.r_pc);
      regcache_collect (ARM_PS_REGNUM, (char *) &inferior_registers.r_cpsr);
    }
  else
    {
      unsigned pc_val;
      unsigned psr_val;

      regcache_collect (ARM_PC_REGNUM, (char *) &pc_val);
      regcache_collect (ARM_PS_REGNUM, (char *) &psr_val);
	  
      pc_val = ADDR_BITS_REMOVE (pc_val);
      psr_val ^= ADDR_BITS_REMOVE (psr_val);

      inferior_registers.r_pc = pc_val | psr_val;
    }

#ifndef CROSS_DEBUGGER
  ret = ptrace (PT_SETREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_registers, 0);

  if (ret < 0)
    warning ("unable to store general registers");
#endif
}

static void
store_fp_register (int regno)
{
  struct fpreg inferior_fp_registers;
#ifndef CROSS_DEBUGGER
  int ret;

  ret = ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_fp_registers, 0);

  if (ret < 0)
    {
      warning ("unable to fetch floating-point registers");
      return;
    }
#endif

  switch (regno)
    {
    case ARM_FPS_REGNUM:
      regcache_collect (ARM_FPS_REGNUM,
			(char *) &inferior_fp_registers.fpr_fpsr);
      break;

    default:
      regcache_collect
	(regno, (char *) &inferior_fp_registers.fpr[regno - ARM_F0_REGNUM]);
      break;
    }

#ifndef CROSS_DEBUGGER
  ret = ptrace (PT_SETFPREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_fp_registers, 0);

  if (ret < 0)
    warning ("unable to write register %d to inferior", regno);
#endif
}

static void
store_fp_regs (void)
{
  struct fpreg inferior_fp_registers;
  int ret;
  int regno;


  for (regno = ARM_F0_REGNUM; regno <= ARM_F7_REGNUM; regno++)
    regcache_collect
      (regno, (char *) &inferior_fp_registers.fpr[regno - ARM_F0_REGNUM]);

  regcache_collect (ARM_FPS_REGNUM, (char *) &inferior_fp_registers.fpr_fpsr);

#ifndef CROSS_DEBUGGER
  ret = ptrace (PT_SETFPREGS, PIDGET (inferior_ptid),
		(PTRACE_ARG3_TYPE) &inferior_fp_registers, 0);

  if (ret < 0)
    warning ("unable to store floating-point registers");
#endif
}

void
store_inferior_registers (int regno)
{
  if (regno >= 0)
    {
      if (regno < ARM_F0_REGNUM || regno > ARM_FPS_REGNUM)
	store_register (regno);
      else
	store_fp_register (regno);
    }
  else
    {
      store_regs ();
      store_fp_regs ();
    }
}


struct md_core
{
  struct reg intreg;
  struct fpreg freg;
};

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size,
		      int which, CORE_ADDR ignore)
{
  struct md_core *core_reg = (struct md_core *) core_reg_sect;
  int regno;
  CORE_ADDR r_pc;

  supply_gregset (&core_reg->intreg);
  supply_fpregset (&core_reg->freg);
}

static void
fetch_elfcore_registers (char *core_reg_sect, unsigned core_reg_size,
			 int which, CORE_ADDR ignore)
{
  struct reg gregset;
  struct fpreg fparegset;

  switch (which)
    {
    case 0:	/* Integer registers.  */
      if (core_reg_size != sizeof (struct reg))
	warning ("wrong size of register set in core file");
      else
	{
	  /* The memcpy may be unnecessary, but we can't really be sure
	     of the alignment of the data in the core file.  */
	  memcpy (&gregset, core_reg_sect, sizeof (gregset));
	  supply_gregset (&gregset);
	}
      break;

    case 2:
      if (core_reg_size != sizeof (struct fpreg))
	warning ("wrong size of FPA register set in core file");
      else
	{
	  /* The memcpy may be unnecessary, but we can't really be sure
	     of the alignment of the data in the core file.  */
	  memcpy (&fparegset, core_reg_sect, sizeof (fparegset));
	  supply_fpregset (&fparegset);
	}
      break;

    default:
      /* Don't know what kind of register request this is; just ignore it.  */
      break;
    }
}

static struct core_fns arm_freebsd_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flovour.  */
  default_check_format,			/* check_format.  */
  default_core_sniffer,			/* core_sniffer.  */
  fetch_core_registers,			/* core_read_registers.  */
  NULL
};

static struct core_fns arm_freebsd_elfcore_fns =
{
  bfd_target_elf_flavour,		/* core_flovour.  */
  default_check_format,			/* check_format.  */
  default_core_sniffer,			/* core_sniffer.  */
  fetch_elfcore_registers,		/* core_read_registers.  */
  NULL
};

void
_initialize_arm_fbsdnat (void)
{
  add_core_fns (&arm_freebsd_core_fns);
  add_core_fns (&arm_freebsd_elfcore_fns);
}
