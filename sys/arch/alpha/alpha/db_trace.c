/*	$OpenBSD: db_trace.c,v 1.24 2024/11/07 16:02:29 miod Exp $	*/

/*
 * Copyright (c) 1997 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1997 Theo de Raadt.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/db_machdep.h>
#include <machine/frame.h>

#include <ddb/db_access.h>
#include <ddb/db_command.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>

extern int	etext;

struct opcode opcode[] = {
	{ OPC_PAL, "call_pal", 0 },	/* 00 */
	{ OPC_RES, "opc01", 0 },	/* 01 */
	{ OPC_RES, "opc02", 0 },	/* 02 */
	{ OPC_RES, "opc03", 0 },	/* 03 */
	{ OPC_RES, "opc04", 0 },	/* 04 */
	{ OPC_RES, "opc05", 0 },	/* 05 */
	{ OPC_RES, "opc06", 0 },	/* 06 */
	{ OPC_RES, "opc07", 0 },	/* 07 */
	{ OPC_MEM, "lda", 1 },		/* 08 */
	{ OPC_MEM, "ldah", 1 },		/* 09 */
	{ OPC_RES, "opc0a", 0 },	/* 0A */
	{ OPC_MEM, "ldq_u", 1 },	/* 0B */
	{ OPC_RES, "opc0c", 0 },	/* 0C */
	{ OPC_RES, "opc0d", 0 },	/* 0D */
	{ OPC_RES, "opc0e", 0 },	/* 0E */
	{ OPC_MEM, "stq_u", 1 },	/* 0F */
	{ OPC_OP, "inta", 0 },		/* 10 */
	{ OPC_OP, "intl", 0 },		/* 11 */
	{ OPC_OP, "ints", 0 },		/* 12 */
	{ OPC_OP, "intm", 0 },		/* 13 */
	{ OPC_RES, "opc14", 0 },	/* 14 */
	{ OPC_OP, "fltv", 1 },		/* 15 */
	{ OPC_OP, "flti", 1 },		/* 16 */
	{ OPC_OP, "fltl", 1 },		/* 17 */
	{ OPC_MEM, "misc", 0 },		/* 18 */
	{ OPC_PAL, "pal19", 0 },	/* 19 */
	{ OPC_MEM, "jsr", 0 },		/* 1A */
	{ OPC_PAL, "pal1b", 0 },	/* 1B */
	{ OPC_RES, "opc1c", 0 },	/* 1C */
	{ OPC_PAL, "pal1d", 0 },	/* 1D */
	{ OPC_PAL, "pal1e", 0 },	/* 1E */
	{ OPC_PAL, "pal1f", 0 },	/* 1F */
	{ OPC_MEM, "ldf", 1 },		/* 20 */
	{ OPC_MEM, "ldg", 1 },		/* 21 */
	{ OPC_MEM, "lds", 1 },		/* 22 */
	{ OPC_MEM, "ldt", 1 },		/* 23 */
	{ OPC_MEM, "stf", 1 },		/* 24 */
	{ OPC_MEM, "stg", 1 },		/* 25 */
	{ OPC_MEM, "sts", 1 },		/* 26 */
	{ OPC_MEM, "stt", 1 },		/* 27 */
	{ OPC_MEM, "ldl", 1 },		/* 28 */
	{ OPC_MEM, "ldq", 1 },		/* 29 */
	{ OPC_MEM, "ldl_l", 1 },	/* 2A */
	{ OPC_MEM, "ldq_l", 1 },	/* 2B */
	{ OPC_MEM, "stl", 1 },		/* 2C */
	{ OPC_MEM, "stq", 1 },		/* 2D */
	{ OPC_MEM, "stl_c", 1 },	/* 2E */
	{ OPC_MEM, "stq_c", 1 },	/* 2F */
	{ OPC_BR, "br", 1 },		/* 30 */
	{ OPC_BR, "fbeq", 1 },		/* 31 */
	{ OPC_BR, "fblt", 1 },		/* 32 */
	{ OPC_BR, "fble", 1 },		/* 33 */
	{ OPC_BR, "bsr", 1 },		/* 34 */
	{ OPC_BR, "fbne", 1 },		/* 35 */
	{ OPC_BR, "fbge", 1 },		/* 36 */
	{ OPC_BR, "fbgt", 1 },		/* 37 */
	{ OPC_BR, "blbc", 1 },		/* 38 */
	{ OPC_BR, "beq", 1 },		/* 39 */
	{ OPC_BR, "blt", 1 },		/* 3A */
	{ OPC_BR, "ble", 1 },		/* 3B */
	{ OPC_BR, "blbs", 1 },		/* 3C */
	{ OPC_BR, "bne", 1 },		/* 3D */
	{ OPC_BR, "bge", 1 },		/* 3E */
	{ OPC_BR, "bgt", 1 },		/* 3F */
};

static __inline int sext(u_int);
static __inline int rega(u_int);
static __inline int regb(u_int);
static __inline int regc(u_int);
static __inline int disp(u_int);

static __inline int
sext(u_int x)
{
	return ((x & 0x8000) ? -(-x & 0xffff) : (x & 0xffff));
}

static __inline int
rega(u_int x)
{
	return ((x >> 21) & 0x1f);
}

static __inline int
regb(u_int x)
{
	return ((x >> 16) & 0x1f);
}

static __inline int
regc(u_int x)
{
	return (x & 0x1f);
}

static __inline int
disp(u_int x)
{
	return (sext(x & 0xffff));
}

/*
 * XXX There are a couple of problems with this code:
 *
 *	The argument list printout code is likely to get confused.
 *
 *	It relies on the conventions of gcc code generation.
 *
 *	It uses heuristics to calculate the framesize, and might get it wrong.
 *
 *	It doesn't yet use the framepointer if available.
 *
 *	The address argument can only be used for pointing at trapframes
 *	since a frame pointer of its own serves no good on the alpha,
 *	you need a pc value too.
 *
 *	The heuristics used for tracing through a trap relies on having
 *	symbols available.
 */
void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	u_long		*frame;
	int		i, framesize;
	vaddr_t		pc, ra;
	u_int		inst;
	const char	*name;
	db_expr_t	offset;
	db_regs_t	*regs;
	u_long		*slot[32];

	bzero(slot, sizeof(slot));
	if (count == -1)
		count = 65535;

	if (have_addr) {
		(*pr)("alpha trace requires a trap frame... giving up.\n");
		return;
	}
	regs = &ddb_regs;
trapframe:
	/* remember where various registers are stored */
	for (i = 0; i < 31; i++)
		slot[i] = &regs->tf_regs[0] +
		    ((u_long *)db_regs[i].valuep - &ddb_regs.tf_regs[0]);
	frame = (u_long *)regs->tf_regs[FRAME_SP];
	pc = (vaddr_t)regs->tf_regs[FRAME_PC];
	ra = (vaddr_t)regs->tf_regs[FRAME_RA];

	while (count-- && pc >= (vaddr_t)KERNBASE && pc < (vaddr_t)&etext) {
		db_find_sym_and_offset(pc, &name, &offset);

		if (name == NULL) {
			(*pr)("%lx(", pc);
			/* Limit the search for procedure start */
			offset = 65536;
		} else {
			(*pr)("%s(", name);
		}

		framesize = 0;
		for (i = sizeof (int); i <= offset; i += sizeof (int)) {
			inst = *(u_int *)(pc - i);

			/*
			 * If by chance we don't have any symbols we have to
			 * get out somehow anyway.  Check for the preceding
			 * procedure return in that case.
			 */
			if (name[0] == '?' && inst_return(inst))
				break;

			/*
			 * Disassemble to get the needed info for the frame.
			 */
			if ((inst & 0xffff0000) == 0x23de0000)
				/* lda sp,n(sp) */
				framesize -= disp(inst) / sizeof (u_long);
			else if ((inst & 0xfc1f0000) == 0xb41e0000)
				/* stq X,n(sp) */
				slot[rega(inst)] =
				    frame + disp(inst) / sizeof (u_long);
			else if ((inst & 0xfc000fe0) == 0x44000400 &&
			    rega(inst) == regb(inst)) {
				/* bis X,X,Y (aka mov X,Y) */
				/* zero is hardwired */
				if (rega(inst) != 31)
					slot[rega(inst)] = slot[regc(inst)];
				slot[regc(inst)] = 0;
				/*
				 * XXX In here we might special case a frame
				 * pointer setup, i.e. mov sp, fp.
				 */
			} else if (db_inst_load(inst))
				/* clobbers a register */
				slot[rega(inst)] = 0;
			else if (opcode[inst >> 26].opc_fmt == OPC_OP)
				/* clobbers a register */
				slot[regc(inst)] = 0;
			/*
			 * XXX Recognize more reg clobbering instructions and
			 * set slot[reg] = 0 then too.
			 */
		}

		/*
		 * Try to print the 6 quads that might hold the args.
		 * We print 6 of them even if there are fewer, cause we don't
		 * know the number.  Maybe we could skip the last ones
		 * that never got used.  If we cannot know the value, print
		 * a question mark.
		 */
		for (i = 0; i < 6; i++) {
			if (i > 0)
				(*pr)(", ");
			if (slot[16 + i])
				(*pr)("%lx", *slot[16 + i]);
			else
				(*pr)("?");
		}

#if 0
		/*
		 * XXX This will go eventually when I trust the argument
		 * printout heuristics.
		 *
		 * Print the stack frame contents.
		 */
		(*pr)(") [%p: ", frame);
		if (framesize > 1) {
			for (i = 0; i < framesize - 1; i++)
				(*pr)("%lx, ", frame[i]);
			(*pr)("%lx", frame[i]);
		}
		(*pr)("] at ");
#else
		(*pr)(") at ");
#endif
		db_printsym(pc, DB_STGY_PROC, pr);
		(*pr)("\n");

		/*
		 * If we are looking at a Xent* routine we are in a trap
		 * context.
		 */
		if (strncmp(name, "Xent", sizeof("Xent") - 1) == 0) {
			regs = (db_regs_t *)frame;
			goto trapframe;
		}

		/* Look for the return address if recorded.  */
		if (slot[26])
			ra = *(vaddr_t *)slot[26];
		else
			break;

		/* Advance to the next frame, if any.  */
		frame += framesize;
		if (ra == pc)
			break;
		pc = ra;
	}
}
