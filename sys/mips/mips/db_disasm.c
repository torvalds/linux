/*	$OpenBSD: db_disasm.c,v 1.1 1998/03/16 09:03:24 pefo Exp $	*/
/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 *	Id: db_disasm.c,v 1.1 1998/03/16 09:03:24 pefo Exp
 *	JNPR: db_disasm.c,v 1.1 2006/08/07 05:38:57 katta
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <vm/vm_param.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <sys/systm.h>

#include <machine/mips_opcode.h>
#include <machine/db_machdep.h>
#include <ddb/ddb.h>
#include <ddb/db_output.h>

static char *op_name[64] = {
/* 0 */ "spec", "bcond","j",	"jal",	"beq",	"bne",	"blez",	"bgtz",
/* 8 */ "addi", "addiu","slti",	"sltiu","andi",	"ori",	"xori",	"lui",
/*16 */ "cop0", "cop1",	"cop2",	"cop3", "beql",	"bnel",	"blezl","bgtzl",
/*24 */ "daddi","daddiu","ldl",	"ldr",	"op34",	"op35",	"op36",	"op37",
/*32 */ "lb",	"lh",	"lwl",	"lw",	"lbu",	"lhu",	"lwr",	"lwu",
/*40 */ "sb",	"sh",	"swl",	"sw",	"sdl",	"sdr",	"swr",	"cache",
/*48 */ "ll",	"lwc1",	"lwc2",	"lwc3", "lld",	"ldc1",	"ldc2",	"ld",
/*56 */ "sc",	"swc1",	"swc2",	"swc3", "scd",	"sdc1",	"sdc2",	"sd"
};

static char *spec_name[64] = {
/* 0 */ "sll",	"spec01","srl", "sra",	"sllv",	"spec05","srlv","srav",
/* 8 */ "jr",	"jalr",	"spec12","spec13","syscall","break","spec16","sync",
/*16 */ "mfhi",	"mthi",	"mflo", "mtlo",	"dsllv","spec25","dsrlv","dsrav",
/*24 */ "mult",	"multu","div",	"divu",	"dmult","dmultu","ddiv","ddivu",
/*32 */ "add",	"addu",	"sub",	"subu",	"and",	"or",	"xor",	"nor",
/*40 */ "spec50","spec51","slt","sltu",	"dadd","daddu","dsub","dsubu",
/*48 */ "tge","tgeu","tlt","tltu","teq","spec65","tne","spec67",
/*56 */ "dsll","spec71","dsrl","dsra","dsll32","spec75","dsrl32","dsra32"
};

static char *bcond_name[32] = {
/* 0 */ "bltz",	"bgez",	"bltzl", "bgezl", "?", "?", "?", "?",
/* 8 */ "tgei",	"tgeiu", "tlti", "tltiu", "teqi", "?", "tnei", "?",
/*16 */ "bltzal", "bgezal", "bltzall", "bgezall", "?", "?", "?", "?",
/*24 */ "?", "?", "?", "?", "?", "?", "?", "?",
};

static char *cop1_name[64] = {
/* 0 */ "fadd",	"fsub",	"fmpy",	"fdiv",	"fsqrt","fabs",	"fmov",	"fneg",
/* 8 */ "fop08","fop09","fop0a","fop0b","fop0c","fop0d","fop0e","fop0f",
/*16 */ "fop10","fop11","fop12","fop13","fop14","fop15","fop16","fop17",
/*24 */ "fop18","fop19","fop1a","fop1b","fop1c","fop1d","fop1e","fop1f",
/*32 */ "fcvts","fcvtd","fcvte","fop23","fcvtw","fop25","fop26","fop27",
/*40 */ "fop28","fop29","fop2a","fop2b","fop2c","fop2d","fop2e","fop2f",
/*48 */ "fcmp.f","fcmp.un","fcmp.eq","fcmp.ueq","fcmp.olt","fcmp.ult",
	"fcmp.ole","fcmp.ule",
/*56 */ "fcmp.sf","fcmp.ngle","fcmp.seq","fcmp.ngl","fcmp.lt","fcmp.nge",
	"fcmp.le","fcmp.ngt"
};

static char *fmt_name[16] = {
	"s",	"d",	"e",	"fmt3",
	"w",	"fmt5",	"fmt6",	"fmt7",
	"fmt8",	"fmt9",	"fmta",	"fmtb",
	"fmtc",	"fmtd",	"fmte",	"fmtf"
};

static char *reg_name[32] = {
	"zero",	"at",	"v0",	"v1",	"a0",	"a1",	"a2",	"a3",
#if defined(__mips_n32) || defined(__mips_n64)
	"a4",	"a5",	"a6",	"a7",	"t0",	"t1",	"t2",	"t3",
#else
	"t0",	"t1",	"t2",	"t3",	"t4",	"t5",	"t6",	"t7",
#endif
	"s0",	"s1",	"s2",	"s3",	"s4",	"s5",	"s6",	"s7",
	"t8",	"t9",	"k0",	"k1",	"gp",	"sp",	"s8",	"ra"
};

static char *c0_opname[64] = {
	"c0op00","tlbr",  "tlbwi", "c0op03","c0op04","c0op05","tlbwr", "c0op07",
	"tlbp",	"c0op11","c0op12","c0op13","c0op14","c0op15","c0op16","c0op17",
	"rfe",	"c0op21","c0op22","c0op23","c0op24","c0op25","c0op26","c0op27",
	"eret","c0op31","c0op32","c0op33","c0op34","c0op35","c0op36","c0op37",
	"c0op40","c0op41","c0op42","c0op43","c0op44","c0op45","c0op46","c0op47",
	"c0op50","c0op51","c0op52","c0op53","c0op54","c0op55","c0op56","c0op57",
	"c0op60","c0op61","c0op62","c0op63","c0op64","c0op65","c0op66","c0op67",
	"c0op70","c0op71","c0op72","c0op73","c0op74","c0op75","c0op77","c0op77",
};

static char *c0_reg[32] = {
	"index","random","tlblo0","tlblo1","context","tlbmask","wired","c0r7",
	"badvaddr","count","tlbhi","c0r11","sr","cause","epc",	"prid",
	"config","lladr","watchlo","watchhi","xcontext","c0r21","c0r22","c0r23",
	"c0r24","c0r25","ecc","cacheerr","taglo","taghi","errepc","c0r31"
};

static int md_printins(int ins, int mdbdot);

db_addr_t
db_disasm(db_addr_t loc, bool altfmt)

{
	int ins;

	if (vtophys((vm_offset_t)loc)) {
		db_read_bytes((vm_offset_t)loc, (size_t)sizeof(int),
		    (char *)&ins);
		md_printins(ins, loc);
	}

	return (loc + sizeof(int));
} 

/* ARGSUSED */
static int
md_printins(int ins, int mdbdot)
{
	InstFmt i;
	int delay = 0;

	i.word = ins;

	switch (i.JType.op) {
	case OP_SPECIAL:
		if (i.word == 0) {
			db_printf("nop");
			break;
		}
		if (i.RType.func == OP_ADDU && i.RType.rt == 0) {
			db_printf("move\t%s,%s",
			    reg_name[i.RType.rd], reg_name[i.RType.rs]);
			break;
		}
		db_printf("%s", spec_name[i.RType.func]);
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
			db_printf("\t%s,%s,%d", reg_name[i.RType.rd],
			    reg_name[i.RType.rt], i.RType.shamt);
			break;

		case OP_SLLV:
		case OP_SRLV:
		case OP_SRAV:
		case OP_DSLLV:
		case OP_DSRLV:
		case OP_DSRAV:
			db_printf("\t%s,%s,%s", reg_name[i.RType.rd],
			    reg_name[i.RType.rt], reg_name[i.RType.rs]);
			break;

		case OP_MFHI:
		case OP_MFLO:
			db_printf("\t%s", reg_name[i.RType.rd]);
			break;

		case OP_JR:
		case OP_JALR:
			delay = 1;
			/* FALLTHROUGH */
		case OP_MTLO:
		case OP_MTHI:
			db_printf("\t%s", reg_name[i.RType.rs]);
			break;

		case OP_MULT:
		case OP_MULTU:
		case OP_DMULT:
		case OP_DMULTU:
		case OP_DIV:
		case OP_DIVU:
		case OP_DDIV:
		case OP_DDIVU:
			db_printf("\t%s,%s",
			    reg_name[i.RType.rs], reg_name[i.RType.rt]);
			break;

		case OP_SYSCALL:
		case OP_SYNC:
			break;

		case OP_BREAK:
			db_printf("\t%d", (i.RType.rs << 5) | i.RType.rt);
			break;

		default:
			db_printf("\t%s,%s,%s", reg_name[i.RType.rd],
			    reg_name[i.RType.rs], reg_name[i.RType.rt]);
		}
		break;

	case OP_BCOND:
		db_printf("%s\t%s,", bcond_name[i.IType.rt],
		    reg_name[i.IType.rs]);
		goto pr_displ;

	case OP_BLEZ:
	case OP_BLEZL:
	case OP_BGTZ:
	case OP_BGTZL:
		db_printf("%s\t%s,", op_name[i.IType.op],
		    reg_name[i.IType.rs]);
		goto pr_displ;

	case OP_BEQ:
	case OP_BEQL:
		if (i.IType.rs == 0 && i.IType.rt == 0) {
			db_printf("b\t");
			goto pr_displ;
		}
		/* FALLTHROUGH */
	case OP_BNE:
	case OP_BNEL:
		db_printf("%s\t%s,%s,", op_name[i.IType.op],
		    reg_name[i.IType.rs], reg_name[i.IType.rt]);
	pr_displ:
		delay = 1;
		db_printf("0x%08x", mdbdot + 4 + ((short)i.IType.imm << 2));
		break;

	case OP_COP0:
		switch (i.RType.rs) {
		case OP_BCx:
		case OP_BCy:
			db_printf("bc0%c\t",
			    "ft"[i.RType.rt & COPz_BC_TF_MASK]);
			goto pr_displ;

		case OP_MT:
			db_printf("mtc0\t%s,%s",
			    reg_name[i.RType.rt], c0_reg[i.RType.rd]);
			break;

		case OP_DMT:
			db_printf("dmtc0\t%s,%s",
			    reg_name[i.RType.rt], c0_reg[i.RType.rd]);
			break;

		case OP_MF:
			db_printf("mfc0\t%s,%s",
			    reg_name[i.RType.rt], c0_reg[i.RType.rd]);
			break;

		case OP_DMF:
			db_printf("dmfc0\t%s,%s",
			    reg_name[i.RType.rt], c0_reg[i.RType.rd]);
			break;

		default:
			db_printf("%s", c0_opname[i.FRType.func]);
		}
		break;

	case OP_COP1:
		switch (i.RType.rs) {
		case OP_BCx:
		case OP_BCy:
			db_printf("bc1%c\t",
			    "ft"[i.RType.rt & COPz_BC_TF_MASK]);
			goto pr_displ;

		case OP_MT:
			db_printf("mtc1\t%s,f%d",
			    reg_name[i.RType.rt], i.RType.rd);
			break;

		case OP_MF:
			db_printf("mfc1\t%s,f%d",
			    reg_name[i.RType.rt], i.RType.rd);
			break;

		case OP_CT:
			db_printf("ctc1\t%s,f%d",
			    reg_name[i.RType.rt], i.RType.rd);
			break;

		case OP_CF:
			db_printf("cfc1\t%s,f%d",
			    reg_name[i.RType.rt], i.RType.rd);
			break;

		default:
			db_printf("%s.%s\tf%d,f%d,f%d",
			    cop1_name[i.FRType.func], fmt_name[i.FRType.fmt],
			    i.FRType.fd, i.FRType.fs, i.FRType.ft);
		}
		break;

	case OP_J:
	case OP_JAL:
		db_printf("%s\t", op_name[i.JType.op]);
		db_printf("0x%8x",(mdbdot & 0xF0000000) | (i.JType.target << 2));
		delay = 1;
		break;

	case OP_LWC1:
	case OP_SWC1:
		db_printf("%s\tf%d,", op_name[i.IType.op], i.IType.rt);
		goto loadstore;

	case OP_LB:
	case OP_LH:
	case OP_LW:
	case OP_LD:
	case OP_LBU:
	case OP_LHU:
	case OP_LWU:
	case OP_SB:
	case OP_SH:
	case OP_SW:
	case OP_SD:
		db_printf("%s\t%s,", op_name[i.IType.op],
		    reg_name[i.IType.rt]);
	loadstore:
		db_printf("%d(%s)", (short)i.IType.imm, reg_name[i.IType.rs]);
		break;

	case OP_ORI:
	case OP_XORI:
		if (i.IType.rs == 0) {
			db_printf("li\t%s,0x%x",
			    reg_name[i.IType.rt], i.IType.imm);
			break;
		}
		/* FALLTHROUGH */
	case OP_ANDI:
		db_printf("%s\t%s,%s,0x%x", op_name[i.IType.op],
		    reg_name[i.IType.rt], reg_name[i.IType.rs], i.IType.imm);
		break;

	case OP_LUI:
		db_printf("%s\t%s,0x%x", op_name[i.IType.op],
		    reg_name[i.IType.rt], i.IType.imm);
		break;

	case OP_ADDI:
	case OP_DADDI:
	case OP_ADDIU:
	case OP_DADDIU:
		if (i.IType.rs == 0) {
			db_printf("li\t%s,%d", reg_name[i.IType.rt],
			    (short)i.IType.imm);
			break;
		}
		/* FALLTHROUGH */
	default:
		db_printf("%s\t%s,%s,%d", op_name[i.IType.op],
		    reg_name[i.IType.rt], reg_name[i.IType.rs],
		    (short)i.IType.imm);
	}
	db_printf("\n");
	return (delay);
}
