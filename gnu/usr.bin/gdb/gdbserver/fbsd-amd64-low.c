/* GNU/FreeBSD/amd64 specific low level interface, for the remote server for GDB.
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

#include <sys/stddef.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <machine/reg.h>

/* Mapping between the general-purpose registers in `struct user'
   format and GDB's register array layout.  */
static int amd64_regmap[] = {
	offsetof(struct reg, r_rax),
	offsetof(struct reg, r_rbx),
	offsetof(struct reg, r_rcx),
	offsetof(struct reg, r_rdx),
	offsetof(struct reg, r_rsi),
	offsetof(struct reg, r_rdi),
	offsetof(struct reg, r_rbp),
	offsetof(struct reg, r_rsp),
	offsetof(struct reg, r_r8),
	offsetof(struct reg, r_r9),
	offsetof(struct reg, r_r10),
	offsetof(struct reg, r_r11),
	offsetof(struct reg, r_r12),
	offsetof(struct reg, r_r13),
	offsetof(struct reg, r_r14),
	offsetof(struct reg, r_r15),
	offsetof(struct reg, r_rip),
	offsetof(struct reg, r_rflags),	/* XXX 64-bit */
	offsetof(struct reg, r_cs),
	offsetof(struct reg, r_ss),
	offsetof(struct reg, r_ds),
	offsetof(struct reg, r_es),
	offsetof(struct reg, r_fs),
	offsetof(struct reg, r_gs),
};
#define	AMD64_NUM_REGS	(sizeof(amd64_regmap) / sizeof(amd64_regmap[0]))

static const char amd64_breakpoint[] = { 0xCC };
#define	AMD64_BP_LEN	1

extern int debug_threads;

static int
amd64_cannot_store_register(int regno)
{

	return (regno >= AMD64_NUM_REGS);
}

static int
amd64_cannot_fetch_register(int regno)
{

	return (regno >= AMD64_NUM_REGS);
}

static void
amd64_fill_gregset(void *buf)
{
	int i;

	for (i = 0; i < AMD64_NUM_REGS; i++)
		collect_register(i, ((char *)buf) + amd64_regmap[i]);
}

static void
amd64_store_gregset(const void *buf)
{
	int i;

	for (i = 0; i < AMD64_NUM_REGS; i++)
		supply_register(i, ((char *)buf) + amd64_regmap[i]);
}

static void
amd64_fill_fpregset(void *buf)
{

	i387_cache_to_fsave(buf);
}

static void
amd64_store_fpregset(const void *buf)
{

	i387_fsave_to_cache(buf);
}

static void
amd64_fill_fpxregset(void *buf)
{  

	i387_cache_to_fxsave(buf);
}

static void
amd64_store_fpxregset(const void *buf)
{

	i387_fxsave_to_cache(buf);
}


struct regset_info target_regsets[] = {
	{
		PT_GETREGS,
		PT_SETREGS,
		sizeof(struct reg),
		GENERAL_REGS,
		amd64_fill_gregset,
		amd64_store_gregset,
	},
#ifdef HAVE_PTRACE_GETFPXREGS
	{
		PTRACE_GETFPXREGS,
		PTRACE_SETFPXREGS,
		sizeof(elf_fpxregset_t),
		EXTENDED_REGS,
		amd64_fill_fpxregset,
		amd64_store_fpxregset,
	},
#endif
	{
		PT_GETFPREGS,
		PT_SETFPREGS,
		sizeof(struct fpreg),
		FP_REGS,
		amd64_fill_fpregset,
		amd64_store_fpregset,
	},
	{
		0,
		0,
		-1,
		-1,
		NULL,
		NULL,
	}
};

static CORE_ADDR
amd64_get_pc(void)
{
	unsigned long pc;

	collect_register_by_name("rip", &pc);

	if (debug_threads)
		fprintf(stderr, "stop pc (before any decrement) is %016lx\n", pc);

	return (pc);
}

static void
amd64_set_pc(CORE_ADDR newpc)
{

	if (debug_threads)
		fprintf(stderr, "set pc to %016lx\n", (long)newpc);
	supply_register_by_name("rip", &newpc);
}

static int
amd64_breakpoint_at(CORE_ADDR pc)
{
	unsigned char c;

	read_inferior_memory(pc, &c, 1);
	if (c == 0xCC)
		return (1);

	return (0);
}

struct fbsd_target_ops the_low_target = {
	AMD64_NUM_REGS,
	amd64_regmap,
	amd64_cannot_fetch_register,
	amd64_cannot_store_register,
	amd64_get_pc,
	amd64_set_pc,
	amd64_breakpoint,
	AMD64_BP_LEN,
	NULL,
	1,
	amd64_breakpoint_at,
};
