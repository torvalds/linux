/*	$OpenBSD: db_disasm.c,v 1.12 2021/03/11 11:16:58 jsg Exp $	*/
/*
 * Copyright (c) 2006, Miodrag Vallat
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * m88k disassembler for use in ddb
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_sym.h>		/* DB_STGY_PROC, db_printsym() */
#include <ddb/db_access.h>	/* db_get_value() */
#include <ddb/db_output.h>	/* db_printf() */
#include <ddb/db_interface.h>

int	oimmed(int, u_int32_t, const char *, vaddr_t);
int	ctrlregs(int, u_int32_t, const char *, vaddr_t);
int	sindou(int, u_int32_t, const char *, vaddr_t);
int	jump(int, u_int32_t, const char *, vaddr_t);
int	instset(int, u_int32_t, const char *, vaddr_t);
int	obranch(int, u_int32_t, const char *, vaddr_t);
int	brcond(int, u_int32_t, const char *, vaddr_t);
int	otrap(int, u_int32_t, const char *, vaddr_t);
int	obit(int, u_int32_t, const char *, vaddr_t);
int	bitman(int, u_int32_t, const char *, vaddr_t);
int	immem(int, u_int32_t, const char *, vaddr_t);
int	nimmem(int, u_int32_t, const char *, vaddr_t);
int	lognim(int, u_int32_t, const char *, vaddr_t);
int	onimmed(int, u_int32_t, const char *, vaddr_t);
int	pinst(int, u_int32_t, const char *, vaddr_t);

void	printcmp(int, u_int);
void	symofset(u_int, u_int, vaddr_t);
const char *cregname(int, u_int, u_int);

/*
 * Common instruction modifiers
 */

static const char *instwidth[] = {
	".d", "  ", ".h", ".b", ".x"	/* see nimmem() for use of last value */
};
static const char *xinstwidth[4] = {
	".d", "  ", ".x", ".?"
};
static const char *cmpname[0x20] = {
	NULL,
	NULL,
	"eq",
	"ne",
	"gt",
	"le",
	"lt",
	"ge",
	"hi",
	"ls",
	"lo",
	"hs",
	"be",
	"nb",
	"he",
	"nh"
};
static const char *condname[0x20] = {
	NULL,
	"gt",	/* 00001 */
	"eq",	/* 00010 */
	"ge",	/* 00011 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"lt",	/* 01100 */
	"ne",	/* 01101 */
	"le"	/* 01110 */
};
static const char sodname[4] = "sdx?";

/*
 * Descriptive control register names
 */

static const char *m88100_ctrlreg[2][64] = {
	{	/* main unit */
		"PID",
		"PSR",
		"EPSR",
		"SSBR",
		"SXIP",
		"SNIP",
		"SFIP",
		"VBR",
		"DMT0",
		"DMD0",
		"DMA0",
		"DMT1",
		"DMD1",
		"DMA1",
		"DMT2",
		"DMD2",
		"DMA2",
		"SR0",
		"SR1",
		"SR2",
		"SR3",
	},
	{	/* SFU1 = FPU */
		"FPECR",
		"FPHS1",
		"FPLS1",
		"FPHS2",
		"FPLS2",
		"FPPT",
		"FPRH",
		"FPRL",
		"FPIT",
		NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL,
		"FPSR",
		"FPCR"
	}
};

static const char *m88110_ctrlreg[2][64] = {
	{	/* main unit */
		"PID",
		"PSR",
		"EPSR",
		NULL,
		"EXIP",
		"ENIP",
		NULL,
		"VBR",
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		"RES1",
		"RES2",
		"SRX",
		"SR0",
		"SR1",
		"SR2",
		"SR3",
		NULL,
		NULL,
		NULL,
		NULL,
		"ICMD",
		"ICTL",
		"ISAR",
		"ISAP",
		"IUAP",
		"IIR",
		"IBP",
		"IPPU",
		"IPPL",
		"ISR",
		"ILAR",
		"IPAR",
		NULL,
		NULL,
		NULL,
		"DCMD",
		"DCTL",
		"DSAR",
		"DSAP",
		"DUAP",
		"DIR",
		"DBP",
		"DPPU",
		"DPPL",
		"DSR",
		"DLAR",
		"DPAR",
	},
	{	/* SFU1 = FPU */
		"FPECR",
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL,
		NULL,
		"FPSR",
		"FPCR"
	}
};

/* print a comparison code */		/* XXX favors cmp vs fcmp or pcmp */
void
printcmp(int cpu, u_int code)
{
	const char *cmp;

	if (cpu == CPU_88100 && code > 11)
		cmp = NULL;
	else
		cmp = cmpname[code];
	if (cmp != NULL)
		db_printf("%s(%d)", cmp, code);
	else
		db_printf("%d", code);
}

const char *
cregname(int cpu, u_int sfu, u_int regno)
{
	static char regbuf[20];
	const char *regname;

	switch (sfu) {
	case 0:	/* main unit */
	case 1:	/* SFU1 = FPU */
		regname = cpu != CPU_88100 ?
		    m88110_ctrlreg[sfu][regno] : m88100_ctrlreg[sfu][regno];
		if (regname == NULL)
			snprintf(regbuf, sizeof regbuf,
			    sfu == 0 ? "cr%d" : "fcr%d", regno);
		else
			snprintf(regbuf, sizeof regbuf,
			    sfu == 0 ? "cr%d (%s)" : "fcr%d (%s)",
			    regno, regname);
		break;
	default:	/* can't happen */
		snprintf(regbuf, sizeof regbuf, "sfu%dcr%d", sfu, regno);
		break;
	}

	return (regbuf);
}

void
symofset(u_int disp, u_int bit, vaddr_t iadr)
{
	vaddr_t addr;

	if (disp & (1 << (bit - 1))) {
		/* negative value */
		addr = iadr + ((disp << 2) | (~0U << bit));
	} else {
		addr = iadr + (disp << 2);
	}
	db_printsym(addr, DB_STGY_PROC, db_printf);
}

/* Handles immediate integer arithmetic instructions */
int
oimmed(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	int32_t Linst = inst & 0xffff;
	u_int32_t H6inst = inst >> 26;
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rd = (inst >> 21) & 0x1f;

	switch (H6inst) {
	case 0x11:	/* and.u */
	case 0x13:	/* mask.u */
	case 0x15:	/* xor.u */
	case 0x17:	/* or.u */
		db_printf("\t%s.u", opcode);
		break;
	default:
		db_printf("\t%s  ", opcode);
		break;
	}
	db_printf("\t\tr%d, r%d, 0x%04x", rd, rs1, Linst);

	return (1);
}

/* Handles instructions dealing with control registers */
int
ctrlregs(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t dir = (inst >> 14) & 0x03;
	u_int32_t sfu = (inst >> 11) & 0x07;
	u_int32_t creg = (inst >> 5) & 0x3f;
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rs2 = (inst >> 0) & 0x1f;

	/* s1 and s2 must match on {,f}{st,x}cr instructions */
	if (rs1 != rs2 && (dir == 0x02 || dir == 0x03))
		return (0);

	db_printf("\t%s\t\t", opcode);

	switch (dir) {
	case 0x01:	/* ldcr, fldcr */
		db_printf("r%d, %s", rd, cregname(cpu, sfu, creg));
		break;
	case 0x02:	/* stcr, fstcr */
		db_printf("r%d, %s", rs1, cregname(cpu, sfu, creg));
		break;
	default:
	case 0x03:	/* xcr, fxcr */
		db_printf("r%d, r%d, %s",
		    rd, rs1, cregname(cpu, sfu, creg));
		break;
	}

	return (1);
}

/* Handles floating point instructions */
int
sindou(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rs2 = inst & 0x1f;
	u_int32_t td = (inst >> 5) & 0x03;
	u_int32_t t2 = (inst >> 7) & 0x03;
	u_int32_t t1 = (inst >> 9) & 0x03;
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t checkbits = (inst >> 11) & 0x0f;
	u_int32_t rf = (inst >> 15) & 0x01;

	/* do not display a specific fcmpu.s encoding as non-existing fcmpu.d */
	if (checkbits == 0x07)
		td = 0;

	/* do not display dot modifiers for mov.x */
	if (checkbits == 0x08) {
		db_printf("\t%s", opcode);
	} else {
		db_printf("\t%s.%c", opcode, sodname[td]);
	}

	switch (checkbits) {
	default:
	case 0x00:	/* fmul */
	case 0x05:	/* fadd */
	case 0x06:	/* fsub */
	case 0x0e:	/* fdiv */
		db_printf("%c%c\t\t", sodname[t1], sodname[t2]);
		if (rf != 0)
			db_printf("x%d,x%d,x%d", rd, rs1, rs2);
		else
			db_printf("r%d,r%d,r%d", rd, rs1, rs2);
		break;
	case 0x01:	/* fcvt */
	case 0x0f:	/* fsqrt */
		db_printf("%c \t\t", sodname[t2]);
		if (rf != 0)
			db_printf("x%d, x%d", rd, rs2);
		else
			db_printf("r%d, r%d", rd, rs2);
		break;
	case 0x04:	/* flt */
		db_printf("%c \t\t", sodname[t2]);
		if ((inst & 0x200) != 0)	/* does not use the RF bit... */
			db_printf("x%d, x%d", rd, rs2);
		else
			db_printf("r%d, r%d", rd, rs2);
		break;
	case 0x07:	/* fcmp, fcmpu */
		db_printf("%c%c\t\t", sodname[t1], sodname[t2]);
		db_printf("r%d, ", rd);
		if (rf != 0)
			db_printf("x%d, x%d", rs1, rs2);
		else
			db_printf("r%d, r%d", rs1, rs2);
		break;
	case 0x08:	/* mov */
		if (rf != 0 && t1 == 0x01) {	/* mov.x, displayed as mov */
			db_printf("   \t\t");
			db_printf("x%d, x%d", rd, rs2);
		} else {
			db_printf(".%c \t\t", sodname[t2]);

			if (t1 == 0)
				db_printf("r%d, x%d", rd, rs2);
			else
				db_printf("x%d, r%d", rd, rs2);
		}
		break;
	case 0x09:	/* int */
	case 0x0a:	/* nint */
	case 0x0b:	/* trnc */
		db_printf("%c \t\t", sodname[t2]);
		if (rf != 0)
			db_printf("r%d, x%d", rd, rs2);
		else
			db_printf("r%d, r%d", rd, rs2);
		break;
	}

	return (1);
}

int
jump(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rs2 = inst & 0x1f;

	db_printf("\t%s", opcode);
	if ((inst & (1 << 10)) != 0)
		db_printf(".n");
	else
		db_printf("  ");
	db_printf("\t\tr%d", rs2);

	return (1);
}

/* Handles ff1, ff0, tbnd and rte instructions */
int
instset(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rs2 = inst & 0x1f;
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t checkbits = (inst >> 10) & 0x3f;
	u_int32_t H6inst = (inst >> 26) & 0x3f;

	db_printf("\t%s", opcode);
	if (H6inst == 0x3e) { /* tbnd with imm16 */
		db_printf("\t\tr%d, 0x%04x", rs1, inst & 0xffff);
	} else {
		switch (checkbits) {
		case 0x3a:	/* ff1 */
		case 0x3b:	/* ff0 */
			db_printf("\t\tr%d,r%d", rd, rs2);
			break;
		case 0x3e:	/* tbnd */
			db_printf("\t\tr%d,r%d", rs1, rs2);
			break;
		case 0x3f:	/* rte, illop */
			if (rs2 != 0)
				db_printf("%d", rs2);
			break;
		}
	}

	return (1);
}

/* Handles unconditional branches */
int
obranch(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t disp = inst & 0x3ffffff;

	db_printf("\t%s", opcode);
	if ((inst & (1 << 26)) != 0)
		db_printf(".n");
	else
		db_printf("  ");
	db_printf("\t\t");
	symofset(disp, 26, iadr);

	return (1);
}

/* Handles branch on conditions instructions */
int
brcond(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t match = (inst >> 21) & 0x1f;
	u_int32_t rs = (inst >> 16) & 0x1f;
	u_int32_t disp = inst & 0xffff;
	int bcnd = ((inst >> 27) & 0x03) == 1;

	/* skip invalid conditions if bcnd */
	if (bcnd && condname[match] == NULL)
		return (0);

	db_printf("\t%s", opcode);
	if ((inst & (1 << 26)) != 0)
		db_printf(".n");
	else
		db_printf("  ");
	db_printf("\t\t");

	if (bcnd)
		db_printf("%s0", condname[match]);
	else
		printcmp(cpu, match);

	db_printf(", r%d, ", rs);
	symofset(disp, 16, iadr);

	return (1);
}

/* Handles trap instructions */
int
otrap(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t vecno = inst & 0x1ff;
	u_int32_t match = (inst >> 21) & 0x1f;
	u_int32_t rs = (inst >> 16) & 0x1f;
	int tcnd = ((inst >> 12) & 0x0f) == 0xe;

	/* skip invalid conditions if tcnd */
	if (tcnd && condname[match] == NULL)
		return (0);

	db_printf("\t%s\t", opcode);
	if (tcnd)
		db_printf("%s0", condname[match]);
	else
		printcmp(cpu, match);
	db_printf(", r%d, 0x%x", rs, vecno);

	return (1);
}

/* Handles 10 bit immediate bit field operations */
int
obit(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rs = (inst >> 16) & 0x1f;
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t width = (inst >> 5) & 0x1f;
	u_int32_t offset = inst & 0x1f;

	db_printf("\t%s\t\tr%d, r%d, ", opcode, rd, rs);
	if (((inst >> 10) & 0x3f) != 0x2a)	/* rot */
		db_printf("%d", width);
	db_printf("<%d>", offset);

	return (1);
}

/* Handles triadic mode bit field instructions */
int
bitman(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t rs2 = inst & 0x1f;

	db_printf("\t%s\t\tr%d, r%d, r%d", opcode, rd, rs1, rs2);

	return (1);
}

/* Handles immediate load/store/exchange instructions */
int
immem(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t rs = (inst >> 16) & 0x1f;
	u_int32_t st_lda = (inst >> 28) & 0x03;
	u_int32_t aryno = (inst >> 26) & 0x03;
	int rf = 0;
	char c = ' ';

	switch (st_lda) {
	case 0x00:
		if ((aryno & 0x02) != 0) {	/* 0x02, 0x03: ld.hu, ld.bu */
			opcode = "ld";
			c = 'u';
		} else {
			if (cpu == CPU_88100) {
				opcode = "xmem";
				if (aryno == 0) {	/* xmem.bu */
					aryno = 3;
					c = 'u';
				}
			} else {
				/* opcode = "ld"; */
				rf = 1;
			}
		}
		break;

	case 0x03:
		if (cpu != CPU_88100) {
			rf = 1;
			switch (st_lda) {
			case 0x00:		/* ld.x */
				aryno = 2;
				break;
			case 0x03:		/* st, st.d, st.x */
				break;
			}
		}
		break;
	}

	db_printf("\t%s%s%c\t\t", opcode,
	    rf != 0 ? xinstwidth[aryno] : instwidth[aryno], c);
	if (rf != 0)
		db_printf("x%d, r%d, ", rd, rs);
	else
		db_printf("r%d, r%d, ", rd, rs);
	db_printf("0x%x", inst & 0xffff);

	return (1);
}

/* Handles triadic mode load/store/exchange instructions */
int
nimmem(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t scaled = (inst >> 9) & 0x01;
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rs2 = inst & 0x1f;
	u_int32_t st_lda = (inst >> 12) & 0x03;
	u_int32_t aryno = (inst >> 10) & 0x03;
	char c = ' ';
	int rf = 0, wt = 0, usr = 0;

	switch (st_lda) {
	case 0x00:
		switch (aryno) {
		case 0x00:			/* xmem.bu */
			aryno = 3;
			c = 'u';
			/* FALLTHROUGH */
		case 0x01:			/* xmem */
			opcode = "xmem";
			break;
		default:
		case 0x02:			/* ld.hu */
		case 0x03:			/* ld.bu */
			opcode = "ld";
			c = 'u';
			break;
		}
		break;
	case 0x01:
		opcode = "ld";
		if (cpu != CPU_88100) {
			if ((inst & (1 << 26)) == 0)
				rf = 1;
		}
		break;
	case 0x02:	/* st */
		if (cpu != CPU_88100) {
			if ((inst & (1 << 26)) == 0)
				rf = 1;
			if ((inst & (1 << 7)) != 0)
				wt = 1;
		}
		break;
	case 0x03:
		if (cpu != CPU_88100) {
			/* cheat instwidth for lda.x */
			if (aryno == 3)
				aryno = 4;
		}
		break;
	}

	if (st_lda != 0x03 && (inst & (1 << 8)) != 0)
		usr = 1;

	db_printf("\t%s%s%c%s%s\t",
	    opcode, rf != 0 ? xinstwidth[aryno] : instwidth[aryno], c,
	    usr != 0 ? ".usr" : "    ", wt != 0 ? ".wt" : "   ");
	if (rf != 0)
		db_printf("x%d, r%d", rd, rs1);
	else
		db_printf("r%d, r%d", rd, rs1);

	if (scaled != 0)
		db_printf("[r%d]", rs2);
	else
		db_printf(", r%d", rs2);

	return (1);
}

/* Handles triadic mode logical instructions */
int
lognim(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rs2 = inst & 0x1f;

	db_printf("\t%s", opcode);
	if ((inst & (1 << 10)) != 0)
		db_printf(".c");

	db_printf("\t\tr%d, r%d, r%d", rd, rs1, rs2);

	return (1);
}

/* Handles triadic mode arithmetic instructions */
int
onimmed(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rs2 = inst & 0x1f;
	u_int32_t carry = (inst >> 8) & 0x03;

	db_printf("\t%s", opcode);

	if ((inst & (1 << 11)) == 0) {
		switch (carry) {
		case 0x01:
			db_printf(".co");
			break;
		case 0x02:
			db_printf(".ci");
			break;
		case 0x03:
			db_printf(".cio");
			break;
		}
	} else {
		if (cpu != CPU_88100 && carry == 0x01)
			db_printf(".d");
	}

	db_printf("\tr%d, r%d, r%d", rd, rs1, rs2);

	return (1);
}

/* Handles 88110 SFU2 instructions */
int
pinst(int cpu, u_int32_t inst, const char *opcode, vaddr_t iadr)
{
	u_int32_t rd = (inst >> 21) & 0x1f;
	u_int32_t rs1 = (inst >> 16) & 0x1f;
	u_int32_t rs2 = inst & 0x1f;
	u_int32_t tfield = (inst >> 5) & 0x03;
	u_int32_t pfunc = (inst >> 11) & 0x1f;
	const char *saturation[] = { NULL, ".u", ".us", ".s" };

	db_printf("\t%s", opcode);

	switch (pfunc) {
	case 0x0c:	/* ppack */
		db_printf(".%d", (inst >> 5) & 0x3c);
		break;
	case 0x0e:	/* prot */
		break;
	default:	/* other instructions have an S field or zero */
	    {
		u_int32_t sfield = (inst >> 7) & 0x03;

		if (sfield != 0)
			db_printf("%s", saturation[sfield]);
	    }
		break;
	}

	if (tfield != 0 || pfunc == 0x0d /* punpk */) {
		if (tfield != 3)
			db_printf(".%c", "nbh"[tfield]);
	}

	switch (pfunc) {
	case 0x0d:	/* punpk */
		db_printf("\tr%d, r%d", rd, rs1);
		break;
	case 0x0e:	/* prot with immediate */
		db_printf("\tr%d, r%d, %d", rd, rs1, (inst >> 5) & 0x3f);
		break;
	default:
		db_printf("\tr%d, r%d, r%d", rd, rs1, rs2);
		break;
	}

	return (1);
}

static const struct opdesc {
	u_int32_t mask, match;
	int (*opfun)(int, u_int32_t, const char *, vaddr_t);
	const char *opcode;
} opdecode_88100[] = {
	/* ORDER IS IMPORTANT BELOW */
	{ 0xf0000000,	0x00000000,	immem,		NULL },	/* xmem/ld */
	{ 0xf0000000,	0x10000000,	immem,		"ld" },
	{ 0xf0000000,	0x20000000,	immem,		"st" },
	{ 0xf0000000,	0x30000000,	immem,		"lda" },

	{ 0xf8000000,	0x40000000,	oimmed,		"and" },
	{ 0xf8000000,	0x48000000,	oimmed,		"mask" },
	{ 0xf8000000,	0x50000000,	oimmed,		"xor" },
	{ 0xf8000000,	0x58000000,	oimmed,		"or" },
	{ 0xfc000000,	0x60000000,	oimmed,		"addu" },
	{ 0xfc000000,	0x64000000,	oimmed,		"subu" },
	{ 0xfc000000,	0x68000000,	oimmed,		"divu" },
	{ 0xfc000000,	0x6c000000,	oimmed,		"mul" },
	{ 0xfc000000,	0x70000000,	oimmed,		"add" },
	{ 0xfc000000,	0x74000000,	oimmed,		"sub" },
	{ 0xfc000000,	0x78000000,	oimmed,		"div" },
	{ 0xfc000000,	0x7c000000,	oimmed,		"cmp" },

	{ 0xfc00f800,	0x80004000,	ctrlregs,	"ldcr" },
	{ 0xfc00f800,	0x80004800,	ctrlregs,	"fldcr" },
	{ 0xfc00f800,	0x80008000,	ctrlregs,	"stcr" },
	{ 0xfc00f800,	0x80008800,	ctrlregs,	"fstcr" },
	{ 0xfc00f800,	0x8000c000,	ctrlregs,	"xcr" },
	{ 0xfc00f800,	0x8000c800,	ctrlregs,	"fxcr" },

	{ 0xfc00f800,	0x84000000,	sindou,		"fmul" },
	{ 0xfc1fff80,	0x84002000,	sindou,		"flt" },
	{ 0xfc00f800,	0x84002800,	sindou,		"fadd" },
	{ 0xfc00f800,	0x84003000,	sindou,		"fsub" },
	{ 0xfc00f860,	0x84003800,	sindou,		"fcmp" },
	{ 0xfc1ffe60,	0x84004800,	sindou,		"int" },
	{ 0xfc1ffe60,	0x84005000,	sindou,		"nint" },
	{ 0xfc1ffe60,	0x84005800,	sindou,		"trnc" },
	{ 0xfc00f800,	0x84007000,	sindou,		"fdiv" },

	{ 0xf8000000,	0xc0000000,	obranch,	"br" },
	{ 0xf8000000,	0xc8000000,	obranch,	"bsr" },

	{ 0xf8000000,	0xd0000000,	brcond,		"bb0" },
	{ 0xf8000000,	0xd8000000,	brcond,		"bb1" },
	{ 0xf8000000,	0xe8000000,	brcond,		"bcnd" },

	{ 0xfc00fc00,	0xf0008000,	obit,		"clr" },
	{ 0xfc00fc00,	0xf0008800,	obit,		"set" },
	{ 0xfc00fc00,	0xf0009000,	obit,		"ext" },
	{ 0xfc00fc00,	0xf0009800,	obit,		"extu" },
	{ 0xfc00fc00,	0xf000a000,	obit,		"mak" },
	{ 0xfc00fc00,	0xf000a800,	obit,		"rot" },

	{ 0xfc00fe00,	0xf000d000,	otrap,		"tb0" },
	{ 0xfc00fe00,	0xf000d800,	otrap,		"tb1" },
	{ 0xfc00fe00,	0xf000e800,	otrap,		"tcnd" },

	{ 0xfc00f0e0,	0xf4000000,	nimmem,		NULL },	/* xmem/ld */
	{ 0xfc00f0e0,	0xf4001000,	nimmem,		"ld" },
	{ 0xfc00f0e0,	0xf4002000,	nimmem,		"st" },
	{ 0xfc00f0e0,	0xf4003000,	nimmem,		"lda" },

	{ 0xfc00fbe0,	0xf4004000,	lognim,		"and" },
	{ 0xfc00fbe0,	0xf4005000,	lognim,		"xor" },
	{ 0xfc00fbe0,	0xf4005800,	lognim,		"or" },

	{ 0xfc00fce0,	0xf4006000,	onimmed,	"addu" },
	{ 0xfc00fce0,	0xf4006400,	onimmed,	"subu" },
	{ 0xfc00fce0,	0xf4006800,	onimmed,	"divu" },
	{ 0xfc00fce0,	0xf4006c00,	onimmed,	"mul" },
	{ 0xfc00fce0,	0xf4007000,	onimmed,	"add" },
	{ 0xfc00fce0,	0xf4007400,	onimmed,	"sub" },
	{ 0xfc00fce0,	0xf4007800,	onimmed,	"div" },
	{ 0xfc00fce0,	0xf4007c00,	onimmed,	"cmp" },

	{ 0xfc00ffe0,	0xf4008000,	bitman,		"clr" },
	{ 0xfc00ffe0,	0xf4008800,	bitman,		"set" },
	{ 0xfc00ffe0,	0xf4009000,	bitman,		"ext" },
	{ 0xfc00ffe0,	0xf4009800,	bitman,		"extu" },
	{ 0xfc00ffe0,	0xf400a000,	bitman,		"mak" },
	{ 0xfc00ffe0,	0xf400a800,	bitman,		"rot" },

	{ 0xfc00fbe0,	0xf400c000,	jump,		"jmp" },
	{ 0xfc00fbe0,	0xf400c800,	jump,		"jsr" },

	{ 0xfc00ffe0,	0xf400e800,	instset,	"ff1" },
	{ 0xfc00ffe0,	0xf400ec00,	instset,	"ff0" },
	{ 0xfc00ffe0,	0xf400f800,	instset,	"tbnd" },
	{ 0xfc00ffe0,	0xf400fc00,	instset,	"rte" },
	{ 0xfc000000,	0xf8000000,	instset,	"tbnd" },
	{ 0,		0,		NULL,		NULL }
}, opdecode_88110[] = {
	/* ORDER IS IMPORTANT BELOW */
	{ 0xe0000000,	0x00000000,	immem,		"ld" },
	{ 0xf0000000,	0x20000000,	immem,		"st" },
	{ 0xfc000000,	0x3c000000,	immem,		"ld" },
	{ 0xf0000000,	0x30000000,	immem,		"st" },

	{ 0xf8000000,	0x40000000,	oimmed,		"and" },
	{ 0xf8000000,	0x48000000,	oimmed,		"mask" },
	{ 0xf8000000,	0x50000000,	oimmed,		"xor" },
	{ 0xf8000000,	0x58000000,	oimmed,		"or" },
	{ 0xfc000000,	0x60000000,	oimmed,		"addu" },
	{ 0xfc000000,	0x64000000,	oimmed,		"subu" },
	{ 0xfc000000,	0x68000000,	oimmed,		"divu" },
	{ 0xfc000000,	0x6c000000,	oimmed,		"mulu" },
	{ 0xfc000000,	0x70000000,	oimmed,		"add" },
	{ 0xfc000000,	0x74000000,	oimmed,		"sub" },
	{ 0xfc000000,	0x78000000,	oimmed,		"divs" },
	{ 0xfc000000,	0x7c000000,	oimmed,		"cmp" },

	{ 0xfc1ff81f,	0x80004000,	ctrlregs,	"ldcr" },
	{ 0xfc1ff81f,	0x80004800,	ctrlregs,	"fldcr" },
	{ 0xffe0f800,	0x80008000,	ctrlregs,	"stcr" },
	{ 0xffe0f800,	0x80008800,	ctrlregs,	"fstcr" },
	{ 0xfc00f800,	0x8000c000,	ctrlregs,	"xcr" },
	{ 0xfc00f800,	0x8000c800,	ctrlregs,	"fxcr" },

	{ 0xfc007800,	0x84000000,	sindou,		"fmul" },
	{ 0xfc1f7e00,	0x84000800,	sindou,		"fcvt" },
	{ 0xfc1ffd80,	0x84002000,	sindou,		"flt" },
	{ 0xfc007800,	0x84002800,	sindou,		"fadd" },
	{ 0xfc007800,	0x84003000,	sindou,		"fsub" },
	{ 0xfc007860,	0x84003800,	sindou,		"fcmp" },
	{ 0xfc007860,	0x84003820,	sindou,		"fcmpu" },
	{ 0xfc1ffe60,	0x8400c000,	sindou,		"mov" },
	{ 0xfc17fe60,	0x84004200,	sindou,		"mov" },
	{ 0xfc1f7e60,	0x84004800,	sindou,		"int" },
	{ 0xfc1f7e60,	0x84005000,	sindou,		"nint" },
	{ 0xfc1f7e60,	0x84005800,	sindou,		"trnc" },
	{ 0xfc007800,	0x84007000,	sindou,		"fdiv" },
	{ 0xfc1f7e00,	0x84007800,	sindou,		"fsqrt" },

	{ 0xfc00ffe0,	0x88000000,	pinst,		"pmul" },
	{ 0xfc00ff80,	0x88002000,	pinst,		"padd" },
	{ 0xfc00fe00,	0x88002000,	pinst,		"padds" },
	{ 0xfc00ff80,	0x88003000,	pinst,		"psub" },
	{ 0xfc00fe00,	0x88003000,	pinst,		"psubs" },
	{ 0xfc00ffe0,	0x88003860,	pinst,		"pcmp" },
	{ 0xfc00f800,	0x88006000,	pinst,		"ppack" },
	{ 0xfc00ff9f,	0x88006800,	pinst,		"punpk" },
	{ 0xfc00f87f,	0x88007000,	pinst,		"prot" },
	{ 0xfc00ffe0,	0x88007800,	pinst,		"prot" },

	{ 0xf8000000,	0xc0000000,	obranch,	"br" },
	{ 0xf8000000,	0xc8000000,	obranch,	"bsr" },

	{ 0xf8000000,	0xd0000000,	brcond,		"bb0" },
	{ 0xf8000000,	0xd8000000,	brcond,		"bb1" },
	{ 0xf8000000,	0xe8000000,	brcond,		"bcnd" },

	{ 0xfc00fc00,	0xf0008000,	obit,		"clr" },
	{ 0xfc00fc00,	0xf0008800,	obit,		"set" },
	{ 0xfc00fc00,	0xf0009000,	obit,		"ext" },
	{ 0xfc00fc00,	0xf0009800,	obit,		"extu" },
	{ 0xfc00fc00,	0xf000a000,	obit,		"mak" },
	{ 0xfc00ffe0,	0xf000a800,	obit,		"rot" },

	{ 0xfc00fe00,	0xf000d000,	otrap,		"tb0" },
	{ 0xfc00fe00,	0xf000d800,	otrap,		"tb1" },
	{ 0xfc00fe00,	0xf000e800,	otrap,		"tcnd" },

	{ 0xfc00f0e0,	0xf4000000,	nimmem,		NULL },	/* ld/xmem */
	{ 0xf800f0e0,	0xf0001000,	nimmem,		"ld" },
	{ 0xf800f060,	0xf0002000,	nimmem,		"st" },
	{ 0xfc00f2e0,	0xf4003200,	nimmem,		"lda" },

	{ 0xfc00fbe0,	0xf4004000,	lognim,		"and" },
	{ 0xfc00fbe0,	0xf4005000,	lognim,		"xor" },
	{ 0xfc00fbe0,	0xf4005800,	lognim,		"or" },

	{ 0xfc00fce0,	0xf4006000,	onimmed,	"addu" },
	{ 0xfc00fce0,	0xf4006400,	onimmed,	"subu" },
	{ 0xfc00fee0,	0xf4006800,	onimmed,	"divu" },
	{ 0xfc00fee0,	0xf4006c00,	onimmed,	"mulu" },
	{ 0xfc00ffe0,	0xf4006e00,	onimmed,	"muls" },
	{ 0xfc00fce0,	0xf4007000,	onimmed,	"add" },
	{ 0xfc00fce0,	0xf4007400,	onimmed,	"sub" },
	{ 0xfc00ffe0,	0xf4007800,	onimmed,	"divs" },
	{ 0xfc00ffe0,	0xf4007c00,	onimmed,	"cmp" },

	{ 0xfc00ffe0,	0xf4008000,	bitman,		"clr" },
	{ 0xfc00ffe0,	0xf4008800,	bitman,		"set" },
	{ 0xfc00ffe0,	0xf4009000,	bitman,		"ext" },
	{ 0xfc00ffe0,	0xf4009800,	bitman,		"extu" },
	{ 0xfc00ffe0,	0xf400a000,	bitman,		"mak" },
	{ 0xfc00ffe0,	0xf400a800,	bitman,		"rot" },

	{ 0xfffffbe0,	0xf400c000,	jump,		"jmp" },
	{ 0xfffffbe0,	0xf400c800,	jump,		"jsr" },

	{ 0xfc1fffe0,	0xf400e800,	instset,	"ff1" },
	{ 0xfc1fffe0,	0xf400ec00,	instset,	"ff0" },
	{ 0xffe0ffe0,	0xf400f800,	instset,	"tbnd" },
	{ 0xffffffff,	0xf400fc00,	instset,	"rte" },
	{ 0xfffffffc,	0xf400fc00,	instset,	"illop" },
	{ 0xffe00000,	0xf8000000,	instset,	"tbnd" },
	{ 0,		0,		NULL,		NULL }
};

void
m88k_print_instruction(int cpu, u_int iadr, u_int32_t inst)
{
	const struct opdesc *p;

	/*
	 * This messes up "or.b" instructions ever so slightly,
	 * but keeps us in sync between routines...
	 */
	if (inst == 0) {
		db_printf("\t.word\t0\n");
	} else {
		p = cpu != CPU_88100 ? opdecode_88110 : opdecode_88100;
		while (p->mask != 0) {
			if ((inst & p->mask) == p->match) {
				if ((*p->opfun)(cpu, inst, p->opcode, iadr)) {
					db_printf("\n");
					return;
				}
				break;
			}
			p++;
		}
		db_printf("\t.word\t0x%x\n", inst);
	}
}

vaddr_t
db_disasm(vaddr_t loc, int altfmt)
{
	int cpu;

	if (altfmt)
		cpu = CPU_IS88100 ? CPU_88110 : CPU_88100;
	else
		cpu = cputyp;

	m88k_print_instruction(cpu, loc, db_get_value(loc, 4, 0));
	return (loc + 4);
}
