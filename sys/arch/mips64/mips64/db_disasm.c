/*	$OpenBSD: db_disasm.c,v 1.21 2024/09/20 02:00:46 jsg Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)kadb.c	8.1 (Berkeley) 6/10/93
 *      $Id: db_disasm.c,v 1.21 2024/09/20 02:00:46 jsg Exp $
 */

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#else
#include <unistd.h>
#include <stdio.h>
#endif

#ifdef _KERNEL
#include <machine/db_machdep.h>
#endif
#include <machine/mips_opcode.h>
#include <machine/regnum.h>

#ifdef _KERNEL
#include <ddb/db_interface.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#else
#define	db_printsym(addr,flags,fn)	(fn)("%p",addr)
#endif

static const char *op_name[64] = {
	NULL,	/* OP_SPECIAL */
	NULL,	/* OP_BCOND */
	"j",
	"jal",
	"beq",
	"bne",
	"blez",
	"bgtz",

	"addi",
	"addiu",
	"slti",
	"sltiu",
	"andi",
	"ori",
	"xori",
	"lui",

	"cop0",
	"cop1",
	"cop2",
	"cop1x",
	"beql",
	"bnel",
	"blezl",
	"bgtzl",

	"daddi",
	"daddiu",
	"ldl",
	"ldr",
	NULL,
	NULL,
	NULL,
	NULL,

	"lb",
	"lh",
	"lwl",
	"lw",
	"lbu",
	"lhu",
	"lwr",
	"lwu",

	"sb",
	"sh",
	"swl",
	"sw",
	"sdl",
	"sdr",
	"swr",
	"cache",

	"ll",
	"lwc1",
	"lwc2",
	"pref",
	"lld",
	"ldc1",
	"ldc2",
	"ld",

	"sc",
	"swc1",
	"swc2",
	"swc3",
	"scd",
	"sdc1",
	"sdc2",
	"sd"
};

static const char *special_name[64] = {
	"sll",
	NULL,
	"srl",
	"sra",
	"sllv",
	NULL,
	"srlv",
	"srav",

	"jr",
	"jalr",
	"movz",
	"movn",
	"syscall",
	"break",
	NULL,
	"sync",

	"mfhi",
	"mthi",
	"mflo",
	"mtlo",
	"dsllv",
	NULL,
	"dsrlv",
	"dsrav",

	"mult",
	"multu",
	"div",
	"divu",
	"dmult",
	"dmultu",
	"ddiv",
	"ddivu",

	"add",
	"addu",
	"sub",
	"subu",
	"and",
	"or",
	"xor",
	"nor",

	NULL,
	NULL,
	"slt",
	"sltu",
	"dadd",
	"daddu",
	"dsub",
	"dsubu",

	"tge",
	"tgeu",
	"tlt",
	"tltu",
	"teq",
	NULL,
	"tne",
	NULL,

	"dsll",
	NULL,
	"dsrl",
	"dsra",
	"dsll32",
	NULL,
	"dsrl32",
	"dsra32"
};

static const char *bcond_name[32] = {
	"bltz",
	"bgez",
	"bltzl",
	"bgezl",
	NULL,
	NULL,
	NULL,
	NULL,

	"tgei",
	"tgeiu",
	"tlti",
	"tltiu",
	"teqi",
	NULL,
	"tnei",
	NULL,

	"bltzal",
	"bgezal",
	"bltzall",
	"bgezall",
	NULL,
	NULL,
	NULL,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"synci"
};

static const char *cop_std[OP_MTH + 1] = {
	"mfc",
	"dmfc",
	"cfc",
	"mfhc",
	"mtc",
	"dmtc",
	"ctc",
	"mthc"
};

static const char *cop1_name[64] = {
	"add",
	"sub",
	"mul",
	"div",
	"sqrt",
	"abs",
	"mov",
	"neg",

	"round.l",
	"trunc.l",
	"ceil.l",
	"floor.l",
	"round.w",
	"trunc.w",
	"ceil.w",
	"floor.w",

	NULL,
	NULL,	/* movf/movt */
	"movz",
	"movn",
	NULL,
	"recip",
	"rsqrt",
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	"cvt.s",
	"cvt.d",
	NULL,
	NULL,
	"cvt.w",
	"cvt.l",
	NULL,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	"c.f",
	"c.un",
	"c.eq",
	"c.ueq",
	"c.olt",
	"c.ult",
	"c.ole",
	"c.ule",

	"c.sf",
	"c.ngle",
	"c.seq",
	"c.ngl",
	"c.lt",
	"c.nge",
	"c.le",
	"c.ngt"
};

static const char *fmt_name[16] = {
	"s",
	"d",
	NULL,
	NULL,
	"w",
	"l",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static const char *cop1x_op4[8] = {
	NULL,
	NULL,
	NULL,
	NULL,
	"madd",
	"msub",
	"nmadd",
	"nmsub"
};

static const char *cop1x_std[32] = {
	"lwxc1",
	"ldxc1",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	"swxc1",
	"sdxc1",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"prefx",

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static const char *reg_name[32] = {
	"zero",	"at",	"v0",	"v1",	"a0",	"a1",	"a2",	"a3",
	"a4",	"a5",	"a6",	"a7",	"t0",	"t1",	"t2",	"t3",
	"s0",	"s1",	"s2",	"s3",	"s4",	"s5",	"s6",	"s7",
	"t8",	"t9",	"k0",	"k1",	"gp",	"sp",	"s8",	"ra"
};

static const char *cop0_miscname[64] = {
	NULL,
	"tlbr",
	"tlbwi",
	NULL,
	NULL,
	NULL,
	"tlbwr",
	NULL,

	"tlbp",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	NULL,	/* rfe */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	"eret",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	"wait"	/* RM5200 */
};

static const char *cop0_tfp_miscname[64] = {
	NULL,
	"tlbr",
	"tlbw",
	NULL,
	NULL,
	NULL,
	"tlbwr",
	NULL,

	"tlbp",
	"dctr",
	"dctw"
};

static const char *cop0_reg0[32] = {
	"Index",
	"Random",
	"EntryLo0",
	"EntryLo1",
	"Context",
	"PageMask",
	"Wired",
	"Info",		/* RM7000 */

	"BadVAddr",
	"Count",
	"EntryHi",
	"Compare",
	"Status",
	"Cause",
	"EPC",
	"PRId",

	"Config",
	"LLAddr",
	"WatchLo",	/* RM7000 Watch1 */
	"WatchHi",	/* RM7000 Watch2 */
	"XContext",
	NULL,
	"PerfControl",	/* RM7000 */
	NULL,

	"WatchMask",	/* RM7000 */
	"PerfCount",	/* RM7000 */
	"ECC",
	"CacheErr",
	"TagLo",
	"TagHi",
	"ErrorEPC",
	NULL
};

static const char *cop0_reg1[32] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	NULL,
	NULL,
	"IPLLo",
	"IPLHi",
	"IntCtl",
	NULL,
	NULL,
	NULL,

	NULL,
	NULL,
	"DErrAddr0",
	"DErrAddr1",
	NULL,
	NULL,
	NULL,
	NULL,
};

int
dbmd_print_insn(uint32_t ins, vaddr_t mdbdot, int (*pr)(const char *, ...))
{
	InstFmt i;
	int delay = 0;
	const char *insn, *descr;

	i.word = ins;
	insn = op_name[i.JType.op];

	switch (i.JType.op) {
	case OP_SPECIAL:
		/* recognize nop/ssnop variants of sll early */
		if (i.word == 0) {
			(*pr)("nop");
			break;
		} else if (i.word == 1 << 6) {
			(*pr)("ssnop");
			break;
		}

		/* display `daddu' involving zero as `move' */
		if (i.RType.func == OP_DADDU && i.RType.rt == 0) {
			(*pr)("move\t%s,%s",
			    reg_name[i.RType.rd], reg_name[i.RType.rs]);
			break;
		}

		if (i.RType.func == OP_MOVCI) {
			(*pr)("mov%c\t%s,%s,%d",
			    i.RType.rt & 1 ? 't' : 'f',
			    reg_name[i.RType.rd], reg_name[i.RType.rs],
			    i.RType.rt >> 2);
			break;
		}

		/* fix ambiguous opcode memonics */
		insn = special_name[i.RType.func];
		switch (i.RType.func) {
		case OP_SRL:
			if (i.RType.rs != 0)
				insn = "rotr";
			break;
		case OP_SRLV:
			if (i.RType.shamt != 0)
				insn = "rotrv";
			break;
		case OP_JR:
			if (i.RType.shamt != 0)
				insn = "jr.hb";
			break;
		case OP_JALR:
			if (i.RType.shamt != 0)
				insn = "jalr.hb";
			break;
		case OP_DSRL:
			if (i.RType.rs != 0)
				insn = "drotr";
			break;
		case OP_DSRLV:
			if (i.RType.shamt != 0)
				insn = "drotrv";
			break;
		}

		if (insn == NULL)
			goto unknown;
		(*pr)("%s", insn);

		switch (i.RType.func) {
		case OP_SLL:
		case OP_SRL:
		case OP_SRA:
		case OP_DSLL:
		case OP_DSRL:
		case OP_DSRA:
		case OP_DSLL32:
		case OP_DSRL32:
		case OP_DSRA32:
			(*pr)("\t%s,%s,%d",
			    reg_name[i.RType.rd], reg_name[i.RType.rt],
			    i.RType.shamt);
			break;
		case OP_SLLV:
		case OP_SRLV:
		case OP_SRAV:
		case OP_DSLLV:
		case OP_DSRLV:
		case OP_DSRAV:
			(*pr)("\t%s,%s,%s",
			    reg_name[i.RType.rd], reg_name[i.RType.rt],
			    reg_name[i.RType.rs]);
			break;
		case OP_MFHI:
		case OP_MFLO:
			(*pr)("\t%s", reg_name[i.RType.rd]);
			break;
		case OP_JALR:
			delay = 1;
			if (i.RType.rd != RA)
				(*pr)("\t%s,%s",
				    reg_name[i.RType.rd], reg_name[i.RType.rs]);
			else
				(*pr)("\t%s", reg_name[i.RType.rs]);
			break;
		case OP_JR:
			delay = 1;
			/* FALLTHROUGH */
		case OP_MTLO:
		case OP_MTHI:
			(*pr)("\t%s", reg_name[i.RType.rs]);
			break;
		case OP_MULT:
		case OP_MULTU:
		case OP_DIV:
		case OP_DIVU:
		case OP_DMULT:
		case OP_DMULTU:
		case OP_DDIV:
		case OP_DDIVU:
		case OP_TGE:
		case OP_TGEU:
		case OP_TLT:
		case OP_TLTU:
		case OP_TEQ:
		case OP_TNE:
			(*pr)("\t%s,%s",
			    reg_name[i.RType.rs], reg_name[i.RType.rt]);
			break;
		case OP_SYSCALL:
			if ((ins >> 6) != 0)
				(*pr)("\t%d", ins >> 6);
			break;
		case OP_SYNC:
			break;
		case OP_BREAK:
			(*pr)("\t%d", ins >> 16);
			break;
		case OP_MOVZ:
		case OP_MOVN:
		default:
			(*pr)("\t%s,%s,%s",
			    reg_name[i.RType.rd], reg_name[i.RType.rs],
			    reg_name[i.RType.rt]);
		}
		break;

	case OP_BCOND:
		insn = bcond_name[i.IType.rt];
		if (insn == NULL)
			goto unknown;
		if (i.IType.rt == 31) {	/* synci */
			(*pr)("%s\t", insn);
			goto loadstore;
		}
		(*pr)("%s\t%s,", insn, reg_name[i.IType.rs]);
		if ((i.IType.rt & 0x18) == 0x08)	/* trap, not branch */
			(*pr)("%d", i.IType.imm);
		else
			goto pr_displ;

	case OP_J:
	case OP_JAL:
		delay = 1;
		(*pr)("%s\t", insn);
		db_printsym((mdbdot & ~0x0fffffffUL) |
		    (vaddr_t)(i.JType.target << 2), DB_STGY_PROC, pr);
		break;

	case OP_BEQ:
	case OP_BEQL:
		if (i.IType.rs == ZERO && i.IType.rt == ZERO) {
			(*pr)("b\t");
			goto pr_displ;
		}
		/* FALLTHROUGH */
	case OP_BNE:
	case OP_BNEL:
		if (i.IType.rt == ZERO) {
			if (i.IType.op == OP_BEQL || i.IType.op == OP_BNEL) {
				/* get the non-l opcode name */
				insn = op_name[i.IType.op & 0x07];
				(*pr)("%szl\t%s,", insn, reg_name[i.IType.rs]);
			} else
				(*pr)("%sz\t%s,", insn, reg_name[i.IType.rs]);
		} else
			(*pr)("%s\t%s,%s,", insn,
			    reg_name[i.IType.rs], reg_name[i.IType.rt]);
pr_displ:
		delay = 1;
		db_printsym(mdbdot + 4 + ((int16_t)i.IType.imm << 2),
		    DB_STGY_PROC, pr);
		break;

	case OP_BLEZ:
	case OP_BGTZ:
	case OP_BLEZL:
	case OP_BGTZL:
		(*pr)("%s\t%s,", insn, reg_name[i.IType.rs]);
		goto pr_displ;

	case OP_ADDI:
	case OP_ADDIU:
	case OP_DADDI:
	case OP_DADDIU:
		if (i.IType.rs == 0) {
			(*pr)("li\t%s,%d",
			    reg_name[i.IType.rt], (int16_t)i.IType.imm);
			break;
		}
		/* FALLTHROUGH */
	case OP_SLTI:
	case OP_SLTIU:
	default:
		if (insn != NULL)
			(*pr)("%s\t%s,%s,%d", insn,
			    reg_name[i.IType.rt], reg_name[i.IType.rs],
			    (int16_t)i.IType.imm);
		else {
unknown:
			(*pr)(".word\t%08x", ins);
		}
		break;

	case OP_ORI:
	case OP_XORI:
		if (i.IType.rs == 0) {
			(*pr)("li\t%s,0x%x", reg_name[i.IType.rt], i.IType.imm);
			break;
		}
		/* FALLTHROUGH */
	case OP_ANDI:
		(*pr)("%s\t%s,%s,0x%x", insn,
		    reg_name[i.IType.rt], reg_name[i.IType.rs], i.IType.imm);
		break;

	case OP_LUI:
		(*pr)("%s\t%s,0x%x", insn, reg_name[i.IType.rt], i.IType.imm);
		break;

	case OP_COP0:
		switch (i.RType.rs) {
		case OP_MF:
		case OP_DMF:
		case OP_MT:
		case OP_DMT:
			if ((i.RType.func & 0x07) != 0) {
				insn = cop_std[i.RType.rs];
				descr = cop0_reg0[i.RType.rd];
				if (descr != NULL)
					(*pr)("%s0\t%s,%d,%d # %s.%d", insn,
					    reg_name[i.RType.rt], i.RType.rd,
					    i.RType.func & 0x07, descr,
					    i.RType.func & 0x07);
				else
					(*pr)("%s0\t%s,%d,%d", insn,
					    reg_name[i.RType.rt], i.RType.rd,
					    i.RType.func & 0x07);
				break;
			}
			/* FALLTHROUGH */
		case OP_CF:
		case OP_CT:
			insn = cop_std[i.RType.rs];
			if (i.RType.rs == OP_CF || i.RType.rs == OP_CT)
				descr = cop0_reg1[i.RType.rd];
			else
				descr = cop0_reg0[i.RType.rd];
			if (descr != NULL)
				(*pr)("%s0\t%s,%d # %s", insn,
				    reg_name[i.RType.rt], i.RType.rd, descr);
			else
				(*pr)("%s0\t%s,%d", insn,
				    reg_name[i.RType.rt], i.RType.rd);
			break;
		case OP_BC:
			(*pr)("bc0%c%c\t",
			    i.RType.rt & COPz_BC_TF_MASK ? 't' : 'f',
			    i.RType.rt & COPz_BCL_TF_MASK ? 'l' : ' ');
			goto pr_displ;
		case OP_C0MISC:
			if (i.FRType.func < nitems(cop0_miscname))
				insn = cop0_miscname[i.FRType.func];
			else
				insn = NULL;
			if (insn == NULL)
				goto unknown;
			else
				(*pr)("%s", insn);
			break;
		case OP_TFP_C0MISC:
			if (i.FRType.func < nitems(cop0_tfp_miscname))
				insn = cop0_tfp_miscname[i.FRType.func];
			else
				insn = NULL;
			if (insn == NULL)
				goto unknown;
			else
				(*pr)("%s", insn);
			break;
		default:
			goto unknown;
		}
		break;

	case OP_COP1:
		switch (i.RType.rs) {
		case OP_MF:
		case OP_DMF:
		case OP_CF:
		case OP_MFH:
		case OP_MT:
		case OP_DMT:
		case OP_CT:
		case OP_MTH:
			insn = cop_std[i.RType.rs];
			(*pr)("%s1\t%s,f%d", insn,
			    reg_name[i.RType.rt], i.RType.rd);
			break;
		case OP_BC:
			(*pr)("bc1%c%c\t",
			    i.RType.rt & COPz_BC_TF_MASK ? 't' : 'f',
			    i.RType.rt & COPz_BCL_TF_MASK ? 'l' : ' ');
			goto pr_displ;
		default:
			if (fmt_name[i.FRType.fmt] == NULL)
				goto unknown;
			if (i.FRType.func == 0x11) {	/* movcf */
				insn = i.FRType.ft & 1 ? "movt" : "movf";
				(*pr)("%s.%s\tf%d,f%d,%d",
				    insn, fmt_name[i.FRType.fmt],
				    i.FRType.fd, i.FRType.fs, i.FRType.ft >> 2);
				break;
			 }
			insn = cop1_name[i.FRType.func];
			if (insn == NULL)
				goto unknown;
			(*pr)("%s.%s\tf%d,f%d,f%d",
			    insn, fmt_name[i.FRType.fmt],
			    i.FRType.fd, i.FRType.fs, i.FRType.ft);
		}
		break;

	case OP_COP2:
		switch (i.RType.rs) {
		case OP_MF:
		case OP_DMF:
		case OP_CF:
		case OP_MFH:
		case OP_MT:
		case OP_DMT:
		case OP_CT:
		case OP_MTH:
			insn = cop_std[i.RType.rs];
			(*pr)("%s2\t%s,f%d", insn,
			    reg_name[i.RType.rt], i.RType.rd);
			break;
		case OP_BC:
			(*pr)("bc2%c%c\t",
			    i.RType.rt & COPz_BC_TF_MASK ? 't' : 'f',
			    i.RType.rt & COPz_BCL_TF_MASK ? 'l' : ' ');
			goto pr_displ;
		default:
			goto unknown;
		}
		break;

	case OP_COP1X:
		switch (i.FQType.op4) {
		case OP_MADD:
		case OP_MSUB:
		case OP_NMADD:
		case OP_NMSUB:
			if (fmt_name[i.FQType.fmt3] == NULL)
				goto unknown;
			insn = cop1x_op4[i.FQType.op4];
			(*pr)("%s.%s\tf%d,f%d,f%d,f%d",
			    insn, fmt_name[i.FQType.fmt3],
			    i.FQType.fd, i.FQType.fr,
			    i.FQType.fs, i.FQType.ft);
			break;
		default:
			insn = cop1x_std[i.FRType.func];
			switch (i.FRType.func) {
			case OP_LWXC1:
			case OP_LDXC1:
				(*pr)("%s\tf%d,%s(%s)", insn,
				    i.FQType.fd, reg_name[i.FQType.ft],
				    reg_name[i.FQType.fr]);
				break;
			case OP_SWXC1:
			case OP_SDXC1:
				(*pr)("%s\tf%d,%s(%s)", insn,
				    i.FQType.fs, reg_name[i.FQType.ft],
				    reg_name[i.FQType.fr]);
				break;
			case OP_PREFX:
				(*pr)("%s\t%d,%s(%s)", insn,
				    i.FQType.fs, reg_name[i.FQType.ft],
				    reg_name[i.FQType.fr]);
				break;
			}
			break;
		}
		break;

	case OP_LDL:
	case OP_LDR:
	case OP_LB:
	case OP_LH:
	case OP_LWL:
	case OP_LW:
	case OP_LBU:
	case OP_LHU:
	case OP_LWR:
	case OP_LWU:
	case OP_SB:
	case OP_SH:
	case OP_SWL:
	case OP_SW:
	case OP_SDL:
	case OP_SDR:
	case OP_SWR:
	case OP_LL:
	case OP_LLD:
	case OP_LD:
	case OP_SC:
	case OP_SCD:
	case OP_SD:
		(*pr)("%s\t%s,", insn, reg_name[i.IType.rt]);
loadstore:
		(*pr)("%d(%s)", (int16_t)i.IType.imm, reg_name[i.IType.rs]);
		break;

	case OP_CACHE:
		(*pr)("%s\t0x%x,", insn, i.IType.rt);
		goto loadstore;
		break;

	case OP_LWC1:
	case OP_LWC2:
	/* case OP_LWC3: superseded with OP_PREF */
	case OP_LDC1:
	case OP_LDC2:
	case OP_SWC1:
	case OP_SWC2:
	case OP_SWC3:
	case OP_SDC1:
	case OP_SDC2:
		(*pr)("%s\tf%d,", insn, i.IType.rt);
		goto loadstore;

	case OP_PREF:
		(*pr)("%s\t%d,", insn, i.IType.rt);
		goto loadstore;
	}
	(*pr)("\n");
	return delay;
}

#ifdef _KERNEL
vaddr_t
db_disasm(vaddr_t loc, int altfmt)
{
	extern uint32_t kdbpeek(vaddr_t);

	if (dbmd_print_insn(kdbpeek(loc), loc, db_printf)) {
		loc += 4;
		db_printsym(loc, DB_STGY_ANY, db_printf);
		db_printf(":\t ");
		dbmd_print_insn(kdbpeek(loc), loc, db_printf);
	}
	loc += 4;
	return loc;
}
#else
/*
 * Simple userspace test program (to confirm the logic never tries to print
 * NULL, to begin with...)
 */
int
main()
{
	uint32_t insn = 0;

	do {
		printf("%08x\t", insn);
		dbmd_print_insn(insn, 0, printf);
		insn++;
		if ((insn & 0x00ffffff) == 0)
			fprintf(stderr, "%08x\n", insn);
	} while (insn != 0);

	return 0;
}
#endif
