/*	$OpenBSD: db_disasm.c,v 1.25 2024/07/09 01:21:19 jsg Exp $	*/
/*	$NetBSD: db_disasm.c,v 1.11 1996/05/03 19:41:58 christos Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 *
 *	Id: db_disasm.c,v 2.6  92/01/03  20:05:00  dbg (CMU)
 */

/*
 * Instruction disassembler.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <machine/db_machdep.h>

#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>
#include <ddb/db_interface.h>

/*
 * Size attributes
 */
#define	BYTE	0
#define	WORD	1
#define	LONG	2
#define	QUAD	3
#define	SNGL	4
#define	DBLR	5
#define	EXTR	6
#define	SDEP	7
#define	NONE	8
#define	RDEP	9

/*
 * Addressing modes
 */
#define	E	1			/* general effective address */
#define	Eind	2			/* indirect address (jump, call) */
#define	Ew	3			/* address, word size */
#define	Eb	4			/* address, byte size */
#define	R	5			/* register, in 'reg' field */
#define	Rw	6			/* word register, in 'reg' field */
#define	Ri	7			/* register in instruction */
#define	S	8			/* segment reg, in 'reg' field */
#define	Si	9			/* segment reg, in instruction */
#define	A	10			/* accumulator */
#define	BX	11			/* (bx) */
#define	CL	12			/* cl, for shifts */
#define	DX	13			/* dx, for IO */
#define	SI	14			/* si */
#define	DI	15			/* di */
#define	CR	16			/* control register */
#define	DR	17			/* debug register */
#define	TR	18			/* test register */
#define	I	19			/* immediate, unsigned */
#define	Is	20			/* immediate, signed */
#define	Ib	21			/* byte immediate, unsigned */
#define	Ibs	22			/* byte immediate, signed */
#define	Iw	23			/* word immediate, unsigned */
#define	O	25			/* direct address */
#define	Db	26			/* byte displacement from EIP */
#define	Dl	27			/* long displacement from EIP */
#define	o1	28			/* constant 1 */
#define	o3	29			/* constant 3 */
#define	OS	30			/* immediate offset/segment */
#define	ST	31			/* FP stack top */
#define	STI	32			/* FP stack */
#define	X	33			/* extended FP op */
#define	XA	34			/* for 'fstcw %ax' */
#define	El	35			/* address, long size */
#define	Ril	36			/* long register in instruction */
#define	Iba	37			/* byte immediate, don't print if 0xa */
#define	MEx	38			/* memory, or an extension op */

struct inst {
	char *	i_name;			/* name */
	short	i_has_modrm;		/* has regmodrm byte */
	short	i_size;			/* operand size */
	int	i_mode;			/* addressing modes */
	void *	i_extra;		/* pointer to extra opcode table */
};

#define	op1(x)		(x)
#define	op2(x,y)	((x)|((y)<<8))
#define	op3(x,y,z)	((x)|((y)<<8)|((z)<<16))

struct finst {
	char *	f_name;			/* name for memory instruction */
	int	f_size;			/* size for memory instruction */
	int	f_rrmode;		/* mode for rr instruction */
	void *	f_rrname;		/* name for rr instruction
					   (or pointer to table) */
};

char *	db_Grp6[] = {
	"sldt",		"str",		"lldt",		"ltr",
	"verr",		"verw",		"",		""
};

struct inst db_Grp7[] = {
	{ "sgdt",   0, NONE, op2(MEx,5), "\0vmcall\0vmlaunch\0vmresume\0vmxoff"},
	{ "sidt",   0, NONE, op2(MEx,4), "monitor\0mwait\0clac\0stac"},
	{ "lgdt",   0, NONE, op2(MEx,7), "xgetbv\0xsetbv\0\0\0vmfunc\0xend\0xtest" },
	{ "lidt",   0, NONE, op1(E),     0 },
	{ "smsw",   0, NONE, op1(E),     0 },
	{ "",       0, NONE, 0,          0 },
	{ "lmsw",   0, NONE, op1(E),     0 },
	{ "invlpg", 0, NONE, op2(MEx,2), "swapgs\0rdtscp" },
};

char *	db_Grp8[] = {
	"",		"",		"",		"",
	"bt",		"bts",		"btr",		"btc"
};

struct inst db_Grp9[] = {
	{ "fxsave",   0, NONE, op1(E),     0 },
	{ "fxrstor",  0, NONE, op1(E),     0 },
	{ "ldmxcsr",  0, NONE, op1(E),     0 },
	{ "stmxcsr",  0, NONE, op1(E),     0 },
	{ "xsave",    0, NONE, op1(E),     0 },
	{ "xrstor",   0, NONE, op2(MEx,1), "lfence" },
	{ "xsaveopt", 0, NONE, op2(MEx,1), "mfence" },
	{ "clflush",  0, NONE, op2(MEx,1), "sfence" },
};

char *	db_GrpA[] = {
	"",		"cmpxchg8b",	"",		"",
	"",		"",		"rdrand",	"rdseed"
};

char *	db_GrpB[] = {
	"xstore-rng",	"xcrypt-ecb",	"xcrypt-cbc",	"xcrypt-ctr",
	"xcrypt-cfb",	"xcrypt-ofb",	"",		""
};

char *	db_GrpC[] = {
	"montmul",	"xsha1",	"xsha256",	"",
	"",		"",		"",		""
};

struct inst db_inst_0f0x[] = {
/*00*/	{ NULL,	   1,  NONE,  op1(Ew),     db_Grp6 },
/*01*/	{ "",	   1,  RDEP,  0,           db_Grp7 },
/*02*/	{ "lar",   1,  LONG,  op2(E,R),    0 },
/*03*/	{ "lsl",   1,  LONG,  op2(E,R),    0 },
/*04*/	{ "",      0, NONE,  0,	      0 },
/*05*/	{ "",      0, NONE,  0,	      0 },
/*06*/	{ "clts",  0, NONE,  0,	      0 },
/*07*/	{ "",      0, NONE,  0,	      0 },

/*08*/	{ "invd",  0, NONE,  0,	      0 },
/*09*/	{ "wbinvd",0, NONE,  0,	      0 },
/*0a*/	{ "",      0, NONE,  0,	      0 },
/*0b*/	{ "",      0, NONE,  0,	      0 },
/*0c*/	{ "",      0, NONE,  0,	      0 },
/*0d*/	{ "",      0, NONE,  0,	      0 },
/*0e*/	{ "",      0, NONE,  0,	      0 },
/*0f*/	{ "",      0, NONE,  0,	      0 },
};

struct inst	db_inst_0f2x[] = {
/*20*/	{ "mov",   1,  LONG,  op2(CR,E),   0 }, /* use E for reg */
/*21*/	{ "mov",   1,  LONG,  op2(DR,E),   0 }, /* since mod == 11 */
/*22*/	{ "mov",   1,  LONG,  op2(E,CR),   0 },
/*23*/	{ "mov",   1,  LONG,  op2(E,DR),   0 },
/*24*/	{ "mov",   1,  LONG,  op2(TR,E),   0 },
/*25*/	{ "",      0, NONE,  0,	      0 },
/*26*/	{ "mov",   1,  LONG,  op2(E,TR),   0 },
/*27*/	{ "",      0, NONE,  0,	      0 },

/*28*/	{ "",      0, NONE,  0,	      0 },
/*29*/	{ "",      0, NONE,  0,	      0 },
/*2a*/	{ "",      0, NONE,  0,	      0 },
/*2b*/	{ "",      0, NONE,  0,	      0 },
/*2c*/	{ "",      0, NONE,  0,	      0 },
/*2d*/	{ "",      0, NONE,  0,	      0 },
/*2e*/	{ "",      0, NONE,  0,	      0 },
/*2f*/	{ "",      0, NONE,  0,	      0 },
};

struct inst	db_inst_0f3x[] = {
/*30*/	{ "wrmsr", 0, NONE,  0,           0 },
/*31*/	{ "rdtsc", 0, NONE,  0,           0 },
/*32*/	{ "rdmsr", 0, NONE,  0,           0 },
/*33*/	{ "rdpmc", 0, NONE,  0,           0 },
/*34*/	{ "",      0, NONE,  0,           0 },
/*35*/	{ "",      0, NONE,  0,           0 },
/*36*/	{ "",      0, NONE,  0,           0 },
/*37*/	{ "",      0, NONE,  0,           0 },

/*38*/	{ "",      0, NONE,  0,           0 },
/*39*/	{ "",      0, NONE,  0,           0 },
/*3a*/	{ "",      0, NONE,  0,           0 },
/*3b*/	{ "",      0, NONE,  0,           0 },
/*3c*/	{ "",      0, NONE,  0,           0 },
/*3d*/	{ "",      0, NONE,  0,           0 },
/*3e*/	{ "",      0, NONE,  0,           0 },
/*3f*/	{ "",      0, NONE,  0,           0 },
};

struct inst	db_inst_0f8x[] = {
/*80*/	{ "jo",    0, NONE,  op1(Dl),     0 },
/*81*/	{ "jno",   0, NONE,  op1(Dl),     0 },
/*82*/	{ "jb",    0, NONE,  op1(Dl),     0 },
/*83*/	{ "jnb",   0, NONE,  op1(Dl),     0 },
/*84*/	{ "jz",    0, NONE,  op1(Dl),     0 },
/*85*/	{ "jnz",   0, NONE,  op1(Dl),     0 },
/*86*/	{ "jbe",   0, NONE,  op1(Dl),     0 },
/*87*/	{ "jnbe",  0, NONE,  op1(Dl),     0 },

/*88*/	{ "js",    0, NONE,  op1(Dl),     0 },
/*89*/	{ "jns",   0, NONE,  op1(Dl),     0 },
/*8a*/	{ "jp",    0, NONE,  op1(Dl),     0 },
/*8b*/	{ "jnp",   0, NONE,  op1(Dl),     0 },
/*8c*/	{ "jl",    0, NONE,  op1(Dl),     0 },
/*8d*/	{ "jnl",   0, NONE,  op1(Dl),     0 },
/*8e*/	{ "jle",   0, NONE,  op1(Dl),     0 },
/*8f*/	{ "jnle",  0, NONE,  op1(Dl),     0 },
};

struct inst	db_inst_0f9x[] = {
/*90*/	{ "seto",  1,  NONE,  op1(Eb),     0 },
/*91*/	{ "setno", 1,  NONE,  op1(Eb),     0 },
/*92*/	{ "setb",  1,  NONE,  op1(Eb),     0 },
/*93*/	{ "setnb", 1,  NONE,  op1(Eb),     0 },
/*94*/	{ "setz",  1,  NONE,  op1(Eb),     0 },
/*95*/	{ "setnz", 1,  NONE,  op1(Eb),     0 },
/*96*/	{ "setbe", 1,  NONE,  op1(Eb),     0 },
/*97*/	{ "setnbe",1,  NONE,  op1(Eb),     0 },

/*98*/	{ "sets",  1,  NONE,  op1(Eb),     0 },
/*99*/	{ "setns", 1,  NONE,  op1(Eb),     0 },
/*9a*/	{ "setp",  1,  NONE,  op1(Eb),     0 },
/*9b*/	{ "setnp", 1,  NONE,  op1(Eb),     0 },
/*9c*/	{ "setl",  1,  NONE,  op1(Eb),     0 },
/*9d*/	{ "setnl", 1,  NONE,  op1(Eb),     0 },
/*9e*/	{ "setle", 1,  NONE,  op1(Eb),     0 },
/*9f*/	{ "setnle",1,  NONE,  op1(Eb),     0 },
};

struct inst	db_inst_0fax[] = {
/*a0*/	{ "push",  0, NONE,  op1(Si),     0 },
/*a1*/	{ "pop",   0, NONE,  op1(Si),     0 },
/*a2*/	{ "cpuid", 0, NONE,  0,	      0 },
/*a3*/	{ "bt",    1,  LONG,  op2(R,E),    0 },
/*a4*/	{ "shld",  1,  LONG,  op3(Ib,R,E), 0 },
/*a5*/	{ "shld",  1,  LONG,  op3(CL,R,E), 0 },
/*a6*/	{ NULL,    1,  NONE,  0,	      db_GrpC },
/*a7*/	{ NULL,    1,  NONE,  0,	      db_GrpB },

/*a8*/	{ "push",  0, NONE,  op1(Si),     0 },
/*a9*/	{ "pop",   0, NONE,  op1(Si),     0 },
/*aa*/	{ "",      0, NONE,  0,	      0 },
/*ab*/	{ "bts",   1,  LONG,  op2(R,E),    0 },
/*ac*/	{ "shrd",  1,  LONG,  op3(Ib,E,R), 0 },
/*ad*/	{ "shrd",  1,  LONG,  op3(CL,E,R), 0 },
/*ae*/	{ "",      1,  RDEP,  op1(E),      db_Grp9 },
/*af*/	{ "imul",  1,  LONG,  op2(E,R),    0 },
};

struct inst	db_inst_0fbx[] = {
/*b0*/	{ "cmpxchg",1, BYTE,	 op2(R, E),   0 },
/*b1*/	{ "cmpxchg",1, LONG,	 op2(R, E),   0 },
/*b2*/	{ "lss",   1,  LONG,  op2(E, R),   0 },
/*b3*/	{ "btr",   1,  LONG,  op2(R, E),   0 },
/*b4*/	{ "lfs",   1,  LONG,  op2(E, R),   0 },
/*b5*/	{ "lgs",   1,  LONG,  op2(E, R),   0 },
/*b6*/	{ "movzb", 1,  LONG,  op2(Eb, R),  0 },
/*b7*/	{ "movzw", 1,  LONG,  op2(Ew, R),  0 },

/*b8*/	{ "",      0, NONE,  0,	      0 },
/*b9*/	{ "",      0, NONE,  0,	      0 },
/*ba*/	{ NULL,    1,  LONG,  op2(Ib, E),  db_Grp8 },
/*bb*/	{ "btc",   1,  LONG,  op2(R, E),   0 },
/*bc*/	{ "bsf",   1,  LONG,  op2(E, R),   0 },
/*bd*/	{ "bsr",   1,  LONG,  op2(E, R),   0 },
/*be*/	{ "movsb", 1,  LONG,  op2(Eb, R),  0 },
/*bf*/	{ "movsw", 1,  LONG,  op2(Ew, R),  0 },
};

struct inst	db_inst_0fcx[] = {
/*c0*/	{ "xadd",  1,  BYTE,	 op2(R, E),   0 },
/*c1*/	{ "xadd",  1,  LONG,	 op2(R, E),   0 },
/*c2*/	{ "",	   0, NONE,	 0,	      0 },
/*c3*/	{ "",	   0, NONE,	 0,	      0 },
/*c4*/	{ "",	   0, NONE,	 0,	      0 },
/*c5*/	{ "",	   0, NONE,	 0,	      0 },
/*c6*/	{ "",	   0, NONE,	 0,	      0 },
/*c7*/	{ NULL,    1,  NONE,  op1(E),      db_GrpA },

/*c8*/	{ "bswap", 0, LONG,  op1(Ril),    0 },
/*c9*/	{ "bswap", 0, LONG,  op1(Ril),    0 },
/*ca*/	{ "bswap", 0, LONG,  op1(Ril),    0 },
/*cb*/	{ "bswap", 0, LONG,  op1(Ril),    0 },
/*cc*/	{ "bswap", 0, LONG,  op1(Ril),    0 },
/*cd*/	{ "bswap", 0, LONG,  op1(Ril),    0 },
/*ce*/	{ "bswap", 0, LONG,  op1(Ril),    0 },
/*cf*/	{ "bswap", 0, LONG,  op1(Ril),    0 },
};

struct inst *db_inst_0f[] = {
	db_inst_0f0x,
	NULL,
	db_inst_0f2x,
	db_inst_0f3x,
	NULL,
	NULL,
	NULL,
	NULL,
	db_inst_0f8x,
	db_inst_0f9x,
	db_inst_0fax,
	db_inst_0fbx,
	db_inst_0fcx,
	NULL,
	NULL,
	NULL
};

char *	db_Esc92[] = {
	"fnop",		"",		"",		"",
	"",		"",		"",		""
};
char *	db_Esc94[] = {
	"fchs",		"fabs",		"",		"",
	"ftst",		"fxam",		"",		""
};
char *	db_Esc95[] = {
	"fld1",		"fldl2t",	"fldl2e",	"fldpi",
	"fldlg2",	"fldln2",	"fldz",		""
};
char *	db_Esc96[] = {
	"f2xm1",	"fyl2x",	"fptan",	"fpatan",
	"fxtract",	"fprem1",	"fdecstp",	"fincstp"
};
char *	db_Esc97[] = {
	"fprem",	"fyl2xp1",	"fsqrt",	"fsincos",
	"frndint",	"fscale",	"fsin",		"fcos"
};

char *	db_Esca5[] = {
	"",		"fucompp",	"",		"",
	"",		"",		"",		""
};

char *	db_Escb4[] = {
	"fneni",	"fndisi",       "fnclex",	"fninit",
	"fsetpm",	"",		"",		""
};

char *	db_Esce3[] = {
	"",		"fcompp",	"",		"",
	"",		"",		"",		""
};

char *	db_Escf4[] = {
	"fnstsw",	"",		"",		"",
	"",		"",		"",		""
};

struct finst db_Esc8[] = {
/*0*/	{ "fadd",   SNGL,  op2(STI,ST),	0 },
/*1*/	{ "fmul",   SNGL,  op2(STI,ST),	0 },
/*2*/	{ "fcom",   SNGL,  op2(STI,ST),	0 },
/*3*/	{ "fcomp",  SNGL,  op2(STI,ST),	0 },
/*4*/	{ "fsub",   SNGL,  op2(STI,ST),	0 },
/*5*/	{ "fsubr",  SNGL,  op2(STI,ST),	0 },
/*6*/	{ "fdiv",   SNGL,  op2(STI,ST),	0 },
/*7*/	{ "fdivr",  SNGL,  op2(STI,ST),	0 },
};

struct finst db_Esc9[] = {
/*0*/	{ "fld",    SNGL,  op1(STI),	0 },
/*1*/	{ "",       NONE,  op1(STI),	"fxch" },
/*2*/	{ "fst",    SNGL,  op1(X),	db_Esc92 },
/*3*/	{ "fstp",   SNGL,  op1(X),	0 },
/*4*/	{ "fldenv", NONE,  op1(X),	db_Esc94 },
/*5*/	{ "fldcw",  NONE,  op1(X),	db_Esc95 },
/*6*/	{ "fnstenv",NONE,  op1(X),	db_Esc96 },
/*7*/	{ "fnstcw", NONE,  op1(X),	db_Esc97 },
};

struct finst db_Esca[] = {
/*0*/	{ "fiadd",  LONG,  0,		0 },
/*1*/	{ "fimul",  LONG,  0,		0 },
/*2*/	{ "ficom",  LONG,  0,		0 },
/*3*/	{ "ficomp", LONG,  0,		0 },
/*4*/	{ "fisub",  LONG,  0,		0 },
/*5*/	{ "fisubr", LONG,  op1(X),	db_Esca5 },
/*6*/	{ "fidiv",  LONG,  0,		0 },
/*7*/	{ "fidivr", LONG,  0,		0 }
};

struct finst db_Escb[] = {
/*0*/	{ "fild",   LONG,  0,		0 },
/*1*/	{ "",       NONE,  0,		0 },
/*2*/	{ "fist",   LONG,  0,		0 },
/*3*/	{ "fistp",  LONG,  0,		0 },
/*4*/	{ "",       WORD,  op1(X),	db_Escb4 },
/*5*/	{ "fld",    EXTR,  0,		0 },
/*6*/	{ "",       WORD,  0,		0 },
/*7*/	{ "fstp",   EXTR,  0,		0 },
};

struct finst db_Escc[] = {
/*0*/	{ "fadd",   DBLR,  op2(ST,STI),	0 },
/*1*/	{ "fmul",   DBLR,  op2(ST,STI),	0 },
/*2*/	{ "fcom",   DBLR,  0,	 	0 },
/*3*/	{ "fcomp",  DBLR,  0,		0 },
/*4*/	{ "fsub",   DBLR,  op2(ST,STI),	"fsubr" },
/*5*/	{ "fsubr",  DBLR,  op2(ST,STI),	"fsub" },
/*6*/	{ "fdiv",   DBLR,  op2(ST,STI),	"fdivr" },
/*7*/	{ "fdivr",  DBLR,  op2(ST,STI),	"fdiv" },
};

struct finst db_Escd[] = {
/*0*/	{ "fld",    DBLR,  op1(STI),	"ffree" },
/*1*/	{ "",       NONE,  0,		0 },
/*2*/	{ "fst",    DBLR,  op1(STI),	0 },
/*3*/	{ "fstp",   DBLR,  op1(STI),	0 },
/*4*/	{ "frstor", NONE,  op1(STI),	"fucom" },
/*5*/	{ "",       NONE,  op1(STI),	"fucomp" },
/*6*/	{ "fnsave", NONE,  0,		0 },
/*7*/	{ "fnstsw", NONE,  0,		0 },
};

struct finst db_Esce[] = {
/*0*/	{ "fiadd",  WORD,  op2(ST,STI),	"faddp" },
/*1*/	{ "fimul",  WORD,  op2(ST,STI),	"fmulp" },
/*2*/	{ "ficom",  WORD,  0,		0 },
/*3*/	{ "ficomp", WORD,  op1(X),	db_Esce3 },
/*4*/	{ "fisub",  WORD,  op2(ST,STI),	"fsubrp" },
/*5*/	{ "fisubr", WORD,  op2(ST,STI),	"fsubp" },
/*6*/	{ "fidiv",  WORD,  op2(ST,STI),	"fdivrp" },
/*7*/	{ "fidivr", WORD,  op2(ST,STI),	"fdivp" },
};

struct finst db_Escf[] = {
/*0*/	{ "fild",   WORD,  0,		0 },
/*1*/	{ "",       WORD,  0,		0 },
/*2*/	{ "fist",   WORD,  0,		0 },
/*3*/	{ "fistp",  WORD,  0,		0 },
/*4*/	{ "fbld",   NONE,  op1(XA),	db_Escf4 },
/*5*/	{ "fild",   QUAD,  0,		0 },
/*6*/	{ "fbstp",  NONE,  0,		0 },
/*7*/	{ "fistp",  QUAD,  0,		0 },
};

struct finst *db_Esc_inst[] = {
	db_Esc8, db_Esc9, db_Esca, db_Escb,
	db_Escc, db_Escd, db_Esce, db_Escf
};

char *	db_Grp1[] = {
	"add",
	"or",
	"adc",
	"sbb",
	"and",
	"sub",
	"xor",
	"cmp"
};

char *	db_Grp2[] = {
	"rol",
	"ror",
	"rcl",
	"rcr",
	"shl",
	"shr",
	"shl",
	"sar"
};

struct inst db_Grp3[] = {
	{ "test",  1, NONE, op2(I,E), 0 },
	{ "test",  1, NONE, op2(I,E), 0 },
	{ "not",   1, NONE, op1(E),   0 },
	{ "neg",   1, NONE, op1(E),   0 },
	{ "mul",   1, NONE, op2(E,A), 0 },
	{ "imul",  1, NONE, op2(E,A), 0 },
	{ "div",   1, NONE, op2(E,A), 0 },
	{ "idiv",  1, NONE, op2(E,A), 0 },
};

struct inst	db_Grp4[] = {
	{ "inc",   1, BYTE, op1(E),   0 },
	{ "dec",   1, BYTE, op1(E),   0 },
	{ "",      1, NONE, 0,	 0 },
	{ "",      1, NONE, 0,	 0 },
	{ "",      1, NONE, 0,	 0 },
	{ "",      1, NONE, 0,	 0 },
	{ "",      1, NONE, 0,	 0 },
	{ "",      1, NONE, 0,	 0 }
};

struct inst	db_Grp5[] = {
	{ "inc",   1, LONG, op1(E),   0 },
	{ "dec",   1, LONG, op1(E),   0 },
	{ "call",  1, NONE, op1(Eind),0 },
	{ "lcall", 1, NONE, op1(Eind),0 },
	{ "jmp",   1, NONE, op1(Eind),0 },
	{ "ljmp",  1, NONE, op1(Eind),0 },
	{ "push",  1, LONG, op1(E),   0 },
	{ "",      1, NONE, 0,	 0 }
};

struct inst db_inst_table[256] = {
/*00*/	{ "add",   1,  BYTE,  op2(R, E),  0 },
/*01*/	{ "add",   1,  LONG,  op2(R, E),  0 },
/*02*/	{ "add",   1,  BYTE,  op2(E, R),  0 },
/*03*/	{ "add",   1,  LONG,  op2(E, R),  0 },
/*04*/	{ "add",   0, BYTE,  op2(I, A),  0 },
/*05*/	{ "add",   0, LONG,  op2(Is, A), 0 },
/*06*/	{ "push",  0, NONE,  op1(Si),    0 },
/*07*/	{ "pop",   0, NONE,  op1(Si),    0 },

/*08*/	{ "or",    1,  BYTE,  op2(R, E),  0 },
/*09*/	{ "or",    1,  LONG,  op2(R, E),  0 },
/*0a*/	{ "or",    1,  BYTE,  op2(E, R),  0 },
/*0b*/	{ "or",    1,  LONG,  op2(E, R),  0 },
/*0c*/	{ "or",    0, BYTE,  op2(I, A),  0 },
/*0d*/	{ "or",    0, LONG,  op2(I, A),  0 },
/*0e*/	{ "push",  0, NONE,  op1(Si),    0 },
/*0f*/	{ "",      0, NONE,  0,	     0 },

/*10*/	{ "adc",   1,  BYTE,  op2(R, E),  0 },
/*11*/	{ "adc",   1,  LONG,  op2(R, E),  0 },
/*12*/	{ "adc",   1,  BYTE,  op2(E, R),  0 },
/*13*/	{ "adc",   1,  LONG,  op2(E, R),  0 },
/*14*/	{ "adc",   0, BYTE,  op2(I, A),  0 },
/*15*/	{ "adc",   0, LONG,  op2(Is, A), 0 },
/*16*/	{ "push",  0, NONE,  op1(Si),    0 },
/*17*/	{ "pop",   0, NONE,  op1(Si),    0 },

/*18*/	{ "sbb",   1,  BYTE,  op2(R, E),  0 },
/*19*/	{ "sbb",   1,  LONG,  op2(R, E),  0 },
/*1a*/	{ "sbb",   1,  BYTE,  op2(E, R),  0 },
/*1b*/	{ "sbb",   1,  LONG,  op2(E, R),  0 },
/*1c*/	{ "sbb",   0, BYTE,  op2(I, A),  0 },
/*1d*/	{ "sbb",   0, LONG,  op2(Is, A), 0 },
/*1e*/	{ "push",  0, NONE,  op1(Si),    0 },
/*1f*/	{ "pop",   0, NONE,  op1(Si),    0 },

/*20*/	{ "and",   1,  BYTE,  op2(R, E),  0 },
/*21*/	{ "and",   1,  LONG,  op2(R, E),  0 },
/*22*/	{ "and",   1,  BYTE,  op2(E, R),  0 },
/*23*/	{ "and",   1,  LONG,  op2(E, R),  0 },
/*24*/	{ "and",   0, BYTE,  op2(I, A),  0 },
/*25*/	{ "and",   0, LONG,  op2(I, A),  0 },
/*26*/	{ "",      0, NONE,  0,	     0 },
/*27*/	{ "daa",   0, NONE,  0,	     0 },

/*28*/	{ "sub",   1,  BYTE,  op2(R, E),  0 },
/*29*/	{ "sub",   1,  LONG,  op2(R, E),  0 },
/*2a*/	{ "sub",   1,  BYTE,  op2(E, R),  0 },
/*2b*/	{ "sub",   1,  LONG,  op2(E, R),  0 },
/*2c*/	{ "sub",   0, BYTE,  op2(I, A),  0 },
/*2d*/	{ "sub",   0, LONG,  op2(Is, A), 0 },
/*2e*/	{ "",      0, NONE,  0,	     0 },
/*2f*/	{ "das",   0, NONE,  0,	     0 },

/*30*/	{ "xor",   1,  BYTE,  op2(R, E),  0 },
/*31*/	{ "xor",   1,  LONG,  op2(R, E),  0 },
/*32*/	{ "xor",   1,  BYTE,  op2(E, R),  0 },
/*33*/	{ "xor",   1,  LONG,  op2(E, R),  0 },
/*34*/	{ "xor",   0, BYTE,  op2(I, A),  0 },
/*35*/	{ "xor",   0, LONG,  op2(I, A),  0 },
/*36*/	{ "",      0, NONE,  0,	     0 },
/*37*/	{ "aaa",   0, NONE,  0,	     0 },

/*38*/	{ "cmp",   1,  BYTE,  op2(R, E),  0 },
/*39*/	{ "cmp",   1,  LONG,  op2(R, E),  0 },
/*3a*/	{ "cmp",   1,  BYTE,  op2(E, R),  0 },
/*3b*/	{ "cmp",   1,  LONG,  op2(E, R),  0 },
/*3c*/	{ "cmp",   0, BYTE,  op2(I, A),  0 },
/*3d*/	{ "cmp",   0, LONG,  op2(Is, A), 0 },
/*3e*/	{ "",      0, NONE,  0,	     0 },
/*3f*/	{ "aas",   0, NONE,  0,	     0 },

/*40*/	{ "inc",   0, LONG,  op1(Ri),    0 },
/*41*/	{ "inc",   0, LONG,  op1(Ri),    0 },
/*42*/	{ "inc",   0, LONG,  op1(Ri),    0 },
/*43*/	{ "inc",   0, LONG,  op1(Ri),    0 },
/*44*/	{ "inc",   0, LONG,  op1(Ri),    0 },
/*45*/	{ "inc",   0, LONG,  op1(Ri),    0 },
/*46*/	{ "inc",   0, LONG,  op1(Ri),    0 },
/*47*/	{ "inc",   0, LONG,  op1(Ri),    0 },

/*48*/	{ "dec",   0, LONG,  op1(Ri),    0 },
/*49*/	{ "dec",   0, LONG,  op1(Ri),    0 },
/*4a*/	{ "dec",   0, LONG,  op1(Ri),    0 },
/*4b*/	{ "dec",   0, LONG,  op1(Ri),    0 },
/*4c*/	{ "dec",   0, LONG,  op1(Ri),    0 },
/*4d*/	{ "dec",   0, LONG,  op1(Ri),    0 },
/*4e*/	{ "dec",   0, LONG,  op1(Ri),    0 },
/*4f*/	{ "dec",   0, LONG,  op1(Ri),    0 },

/*50*/	{ "push",  0, LONG,  op1(Ri),    0 },
/*51*/	{ "push",  0, LONG,  op1(Ri),    0 },
/*52*/	{ "push",  0, LONG,  op1(Ri),    0 },
/*53*/	{ "push",  0, LONG,  op1(Ri),    0 },
/*54*/	{ "push",  0, LONG,  op1(Ri),    0 },
/*55*/	{ "push",  0, LONG,  op1(Ri),    0 },
/*56*/	{ "push",  0, LONG,  op1(Ri),    0 },
/*57*/	{ "push",  0, LONG,  op1(Ri),    0 },

/*58*/	{ "pop",   0, LONG,  op1(Ri),    0 },
/*59*/	{ "pop",   0, LONG,  op1(Ri),    0 },
/*5a*/	{ "pop",   0, LONG,  op1(Ri),    0 },
/*5b*/	{ "pop",   0, LONG,  op1(Ri),    0 },
/*5c*/	{ "pop",   0, LONG,  op1(Ri),    0 },
/*5d*/	{ "pop",   0, LONG,  op1(Ri),    0 },
/*5e*/	{ "pop",   0, LONG,  op1(Ri),    0 },
/*5f*/	{ "pop",   0, LONG,  op1(Ri),    0 },

/*60*/	{ "pusha", 0, LONG,  0,	     0 },
/*61*/	{ "popa",  0, LONG,  0,	     0 },
/*62*/	{ "bound", 1,  LONG,  op2(E, R),  0 },
/*63*/	{ "arpl",  1,  NONE,  op2(Rw,Ew), 0 },
/*64*/	{ "",      0, NONE,  0,	     0 },
/*65*/	{ "",      0, NONE,  0,	     0 },
/*66*/	{ "",      0, NONE,  0,	     0 },
/*67*/	{ "",      0, NONE,  0,	     0 },

/*68*/	{ "push",  0, LONG,  op1(I),     0 },
/*69*/	{ "imul",  1,  LONG,  op3(I,E,R), 0 },
/*6a*/	{ "push",  0, LONG,  op1(Ibs),   0 },
/*6b*/	{ "imul",  1,  LONG,  op3(Ibs,E,R),0 },
/*6c*/	{ "ins",   0, BYTE,  op2(DX, DI), 0 },
/*6d*/	{ "ins",   0, LONG,  op2(DX, DI), 0 },
/*6e*/	{ "outs",  0, BYTE,  op2(SI, DX), 0 },
/*6f*/	{ "outs",  0, LONG,  op2(SI, DX), 0 },

/*70*/	{ "jo",    0, NONE,  op1(Db),     0 },
/*71*/	{ "jno",   0, NONE,  op1(Db),     0 },
/*72*/	{ "jb",    0, NONE,  op1(Db),     0 },
/*73*/	{ "jnb",   0, NONE,  op1(Db),     0 },
/*74*/	{ "jz",    0, NONE,  op1(Db),     0 },
/*75*/	{ "jnz",   0, NONE,  op1(Db),     0 },
/*76*/	{ "jbe",   0, NONE,  op1(Db),     0 },
/*77*/	{ "jnbe",  0, NONE,  op1(Db),     0 },

/*78*/	{ "js",    0, NONE,  op1(Db),     0 },
/*79*/	{ "jns",   0, NONE,  op1(Db),     0 },
/*7a*/	{ "jp",    0, NONE,  op1(Db),     0 },
/*7b*/	{ "jnp",   0, NONE,  op1(Db),     0 },
/*7c*/	{ "jl",    0, NONE,  op1(Db),     0 },
/*7d*/	{ "jnl",   0, NONE,  op1(Db),     0 },
/*7e*/	{ "jle",   0, NONE,  op1(Db),     0 },
/*7f*/	{ "jnle",  0, NONE,  op1(Db),     0 },

/*80*/	{ NULL,	   1,  BYTE,  op2(I, E),   db_Grp1 },
/*81*/	{ NULL,	   1,  LONG,  op2(I, E),   db_Grp1 },
/*82*/	{ NULL,	   1,  BYTE,  op2(I, E),   db_Grp1 },
/*83*/	{ NULL,	   1,  LONG,  op2(Ibs,E),  db_Grp1 },
/*84*/	{ "test",  1,  BYTE,  op2(R, E),   0 },
/*85*/	{ "test",  1,  LONG,  op2(R, E),   0 },
/*86*/	{ "xchg",  1,  BYTE,  op2(R, E),   0 },
/*87*/	{ "xchg",  1,  LONG,  op2(R, E),   0 },

/*88*/	{ "mov",   1,  BYTE,  op2(R, E),   0 },
/*89*/	{ "mov",   1,  LONG,  op2(R, E),   0 },
/*8a*/	{ "mov",   1,  BYTE,  op2(E, R),   0 },
/*8b*/	{ "mov",   1,  LONG,  op2(E, R),   0 },
/*8c*/	{ "mov",   1,  NONE,  op2(S, Ew),  0 },
/*8d*/	{ "lea",   1,  LONG,  op2(E, R),   0 },
/*8e*/	{ "mov",   1,  NONE,  op2(Ew, S),  0 },
/*8f*/	{ "pop",   1,  LONG,  op1(E),      0 },

/*90*/	{ "nop",   0, NONE,  0,	      0 },
/*91*/	{ "xchg",  0, LONG,  op2(A, Ri),  0 },
/*92*/	{ "xchg",  0, LONG,  op2(A, Ri),  0 },
/*93*/	{ "xchg",  0, LONG,  op2(A, Ri),  0 },
/*94*/	{ "xchg",  0, LONG,  op2(A, Ri),  0 },
/*95*/	{ "xchg",  0, LONG,  op2(A, Ri),  0 },
/*96*/	{ "xchg",  0, LONG,  op2(A, Ri),  0 },
/*97*/	{ "xchg",  0, LONG,  op2(A, Ri),  0 },

/*98*/	{ "cbw",   0, SDEP,  0,	      "cwde" },	/* cbw/cwde */
/*99*/	{ "cwd",   0, SDEP,  0,	      "cdq"  },	/* cwd/cdq */
/*9a*/	{ "lcall", 0, NONE,  op1(OS),     0 },
/*9b*/	{ "wait",  0, NONE,  0,	      0 },
/*9c*/	{ "pushf", 0, LONG,  0,	      0 },
/*9d*/	{ "popf",  0, LONG,  0,	      0 },
/*9e*/	{ "sahf",  0, NONE,  0,	      0 },
/*9f*/	{ "lahf",  0, NONE,  0,	      0 },

/*a0*/	{ "mov",   0, BYTE,  op2(O, A),   0 },
/*a1*/	{ "mov",   0, LONG,  op2(O, A),   0 },
/*a2*/	{ "mov",   0, BYTE,  op2(A, O),   0 },
/*a3*/	{ "mov",   0, LONG,  op2(A, O),   0 },
/*a4*/	{ "movs",  0, BYTE,  op2(SI,DI),  0 },
/*a5*/	{ "movs",  0, LONG,  op2(SI,DI),  0 },
/*a6*/	{ "cmps",  0, BYTE,  op2(SI,DI),  0 },
/*a7*/	{ "cmps",  0, LONG,  op2(SI,DI),  0 },

/*a8*/	{ "test",  0, BYTE,  op2(I, A),   0 },
/*a9*/	{ "test",  0, LONG,  op2(I, A),   0 },
/*aa*/	{ "stos",  0, BYTE,  op1(DI),     0 },
/*ab*/	{ "stos",  0, LONG,  op1(DI),     0 },
/*ac*/	{ "lods",  0, BYTE,  op1(SI),     0 },
/*ad*/	{ "lods",  0, LONG,  op1(SI),     0 },
/*ae*/	{ "scas",  0, BYTE,  op1(SI),     0 },
/*af*/	{ "scas",  0, LONG,  op1(SI),     0 },

/*b0*/	{ "mov",   0, BYTE,  op2(I, Ri),  0 },
/*b1*/	{ "mov",   0, BYTE,  op2(I, Ri),  0 },
/*b2*/	{ "mov",   0, BYTE,  op2(I, Ri),  0 },
/*b3*/	{ "mov",   0, BYTE,  op2(I, Ri),  0 },
/*b4*/	{ "mov",   0, BYTE,  op2(I, Ri),  0 },
/*b5*/	{ "mov",   0, BYTE,  op2(I, Ri),  0 },
/*b6*/	{ "mov",   0, BYTE,  op2(I, Ri),  0 },
/*b7*/	{ "mov",   0, BYTE,  op2(I, Ri),  0 },

/*b8*/	{ "mov",   0, LONG,  op2(I, Ri),  0 },
/*b9*/	{ "mov",   0, LONG,  op2(I, Ri),  0 },
/*ba*/	{ "mov",   0, LONG,  op2(I, Ri),  0 },
/*bb*/	{ "mov",   0, LONG,  op2(I, Ri),  0 },
/*bc*/	{ "mov",   0, LONG,  op2(I, Ri),  0 },
/*bd*/	{ "mov",   0, LONG,  op2(I, Ri),  0 },
/*be*/	{ "mov",   0, LONG,  op2(I, Ri),  0 },
/*bf*/	{ "mov",   0, LONG,  op2(I, Ri),  0 },

/*c0*/	{ NULL,	   1,  BYTE,  op2(Ib, E),  db_Grp2 },
/*c1*/	{ NULL,	   1,  LONG,  op2(Ib, E),  db_Grp2 },
/*c2*/	{ "ret",   0, NONE,  op1(Iw),     0 },
/*c3*/	{ "ret",   0, NONE,  0,	      0 },
/*c4*/	{ "les",   1,  LONG,  op2(E, R),   0 },
/*c5*/	{ "lds",   1,  LONG,  op2(E, R),   0 },
/*c6*/	{ "mov",   1,  BYTE,  op2(I, E),   0 },
/*c7*/	{ "mov",   1,  LONG,  op2(I, E),   0 },

/*c8*/	{ "enter", 0, NONE,  op2(Iw, Ib), 0 },
/*c9*/	{ "leave", 0, NONE,  0,           0 },
/*ca*/	{ "lret",  0, NONE,  op1(Iw),     0 },
/*cb*/	{ "lret",  0, NONE,  0,	      0 },
/*cc*/	{ "int",   0, NONE,  op1(o3),     0 },
/*cd*/	{ "int",   0, NONE,  op1(Ib),     0 },
/*ce*/	{ "into",  0, NONE,  0,	      0 },
/*cf*/	{ "iret",  0, NONE,  0,	      0 },

/*d0*/	{ NULL,	   1,  BYTE,  op2(o1, E),  db_Grp2 },
/*d1*/	{ NULL,	   1,  LONG,  op2(o1, E),  db_Grp2 },
/*d2*/	{ NULL,	   1,  BYTE,  op2(CL, E),  db_Grp2 },
/*d3*/	{ NULL,	   1,  LONG,  op2(CL, E),  db_Grp2 },
/*d4*/	{ "aam",   1,  NONE,  op1(Iba),    0 },
/*d5*/	{ "aad",   1,  NONE,  op1(Iba),    0 },
/*d6*/	{ ".byte\t0xd6",0, NONE, 0,       0 },
/*d7*/	{ "xlat",  0, BYTE,  op1(BX),     0 },

/* d8 to df block is ignored: direct test in code handles them */
/*d8*/	{ "",      1,  NONE,  0,	      db_Esc8 },
/*d9*/	{ "",      1,  NONE,  0,	      db_Esc9 },
/*da*/	{ "",      1,  NONE,  0,	      db_Esca },
/*db*/	{ "",      1,  NONE,  0,	      db_Escb },
/*dc*/	{ "",      1,  NONE,  0,	      db_Escc },
/*dd*/	{ "",      1,  NONE,  0,	      db_Escd },
/*de*/	{ "",      1,  NONE,  0,	      db_Esce },
/*df*/	{ "",      1,  NONE,  0,	      db_Escf },

/*e0*/	{ "loopne",0, NONE,  op1(Db),     0 },
/*e1*/	{ "loope", 0, NONE,  op1(Db),     0 },
/*e2*/	{ "loop",  0, NONE,  op1(Db),     0 },
/*e3*/	{ "jcxz",  0, SDEP,  op1(Db),     "jecxz" },
/*e4*/	{ "in",    0, BYTE,  op2(Ib, A),  0 },
/*e5*/	{ "in",    0, LONG,  op2(Ib, A) , 0 },
/*e6*/	{ "out",   0, BYTE,  op2(A, Ib),  0 },
/*e7*/	{ "out",   0, LONG,  op2(A, Ib) , 0 },

/*e8*/	{ "call",  0, NONE,  op1(Dl),     0 },
/*e9*/	{ "jmp",   0, NONE,  op1(Dl),     0 },
/*ea*/	{ "ljmp",  0, NONE,  op1(OS),     0 },
/*eb*/	{ "jmp",   0, NONE,  op1(Db),     0 },
/*ec*/	{ "in",    0, BYTE,  op2(DX, A),  0 },
/*ed*/	{ "in",    0, LONG,  op2(DX, A) , 0 },
/*ee*/	{ "out",   0, BYTE,  op2(A, DX),  0 },
/*ef*/	{ "out",   0, LONG,  op2(A, DX) , 0 },

/*f0*/	{ "",      0, NONE,  0,	     0 },
/*f1*/	{ "",      0, NONE,  0,	     0 },
/*f2*/	{ "",      0, NONE,  0,	     0 },
/*f3*/	{ "",      0, NONE,  0,	     0 },
/*f4*/	{ "hlt",   0, NONE,  0,	     0 },
/*f5*/	{ "cmc",   0, NONE,  0,	     0 },
/*f6*/	{ "",      1,  BYTE,  0,	     db_Grp3 },
/*f7*/	{ "",	   1,  LONG,  0,	     db_Grp3 },

/*f8*/	{ "clc",   0, NONE,  0,	     0 },
/*f9*/	{ "stc",   0, NONE,  0,	     0 },
/*fa*/	{ "cli",   0, NONE,  0,	     0 },
/*fb*/	{ "sti",   0, NONE,  0,	     0 },
/*fc*/	{ "cld",   0, NONE,  0,	     0 },
/*fd*/	{ "std",   0, NONE,  0,	     0 },
/*fe*/	{ "",	   1,  RDEP,  0,	     db_Grp4 },
/*ff*/	{ "",	   1,  RDEP,  0,	     db_Grp5 },
};

struct inst	db_bad_inst =
	{ "???",   0, NONE,  0,	      0 }
;

#define	f_mod(byte)	((byte)>>6)
#define	f_reg(byte)	(((byte)>>3)&0x7)
#define	f_rm(byte)	((byte)&0x7)

#define	sib_ss(byte)	((byte)>>6)
#define	sib_index(byte)	(((byte)>>3)&0x7)
#define	sib_base(byte)	((byte)&0x7)

struct i_addr {
	int		is_reg;	/* if reg, reg number is in 'disp' */
	int		disp;
	char *		base;
	char *		index;
	int		ss;
};

char *	db_index_reg_16[8] = {
	"%bx,%si",
	"%bx,%di",
	"%bp,%si",
	"%bp,%di",
	"%si",
	"%di",
	"%bp",
	"%bx"
};

char *	db_reg[3][8] = {
	{ "%al",  "%cl",  "%dl",  "%bl",  "%ah",  "%ch",  "%dh",  "%bh" },
	{ "%ax",  "%cx",  "%dx",  "%bx",  "%sp",  "%bp",  "%si",  "%di" },
	{ "%eax", "%ecx", "%edx", "%ebx", "%esp", "%ebp", "%esi", "%edi" }
};

char *	db_seg_reg[8] = {
	"%es", "%cs", "%ss", "%ds", "%fs", "%gs", "", ""
};

/*
 * lengths for size attributes
 */
int db_lengths[] = {
	1,	/* BYTE */
	2,	/* WORD */
	4,	/* LONG */
	8,	/* QUAD */
	4,	/* SNGL */
	8,	/* DBLR */
	10,	/* EXTR */
};

#define	get_value_inc(result, loc, size, is_signed) \
	do { \
		result = db_get_value((loc), (size), (is_signed)); \
		(loc) += (size); \
	} while (0)


vaddr_t db_read_address(vaddr_t, int, int, struct i_addr *);
void db_print_address(char *, int, struct i_addr *);
vaddr_t db_disasm_esc(vaddr_t, int, int, int, char *);

/*
 * Read address at location and return updated location.
 */
vaddr_t
db_read_address(vaddr_t loc, int short_addr, int regmodrm,
    struct i_addr *addrp)
{
	int		mod, rm, sib, index, disp;

	mod = f_mod(regmodrm);
	rm  = f_rm(regmodrm);

	if (mod == 3) {
		addrp->is_reg = 1;
		addrp->disp = rm;
		return (loc);
	}
	addrp->is_reg = 0;
	addrp->index = 0;

	if (short_addr) {
		addrp->index = 0;
		addrp->ss = 0;
		switch (mod) {
		    case 0:
			if (rm == 6) {
				get_value_inc(disp, loc, 2, 0);
				addrp->disp = disp;
				addrp->base = 0;
			} else {
				addrp->disp = 0;
				addrp->base = db_index_reg_16[rm];
			}
			break;
		    case 1:
			get_value_inc(disp, loc, 1, 1);
			disp &= 0xffff;
			addrp->disp = disp;
			addrp->base = db_index_reg_16[rm];
			break;
		    case 2:
			get_value_inc(disp, loc, 2, 0);
			addrp->disp = disp;
			addrp->base = db_index_reg_16[rm];
			break;
		}
	} else {
		if (rm == 4) {
			get_value_inc(sib, loc, 1, 0);
			rm = sib_base(sib);
			index = sib_index(sib);
			if (index != 4)
				addrp->index = db_reg[LONG][index];
			addrp->ss = sib_ss(sib);
		}

		switch (mod) {
		    case 0:
			if (rm == 5) {
				get_value_inc(addrp->disp, loc, 4, 0);
				addrp->base = 0;
			} else {
				addrp->disp = 0;
				addrp->base = db_reg[LONG][rm];
			}
			break;
		    case 1:
			get_value_inc(disp, loc, 1, 1);
			addrp->disp = disp;
			addrp->base = db_reg[LONG][rm];
			break;
		    case 2:
			get_value_inc(disp, loc, 4, 0);
			addrp->disp = disp;
			addrp->base = db_reg[LONG][rm];
			break;
		}
	}
	return (loc);
}

void
db_print_address(char *seg, int size, struct i_addr *addrp)
{
	if (addrp->is_reg) {
		db_printf("%s", db_reg[size][addrp->disp]);
		return;
	}

	if (seg)
		db_printf("%s:", seg);

	db_printsym((vaddr_t)addrp->disp, DB_STGY_ANY, db_printf);
	if (addrp->base != 0 || addrp->index != 0) {
		db_printf("(");
		if (addrp->base)
			db_printf("%s", addrp->base);
		if (addrp->index)
			db_printf(",%s,%d", addrp->index, 1<<addrp->ss);
		db_printf(")");
	}
}

/*
 * Disassemble floating-point ("escape") instruction
 * and return updated location.
 */
vaddr_t
db_disasm_esc(vaddr_t loc, int inst, int short_addr, int size, char *seg)
{
	int		regmodrm;
	struct finst	*fp;
	int		mod;
	struct i_addr	address;
	char *		name;

	get_value_inc(regmodrm, loc, 1, 0);
	fp = &db_Esc_inst[inst - 0xd8][f_reg(regmodrm)];
	mod = f_mod(regmodrm);
	if (mod != 3) {
		if (*fp->f_name == '\0') {
			db_printf("<bad instruction>");
			return (loc);
		}

		/*
		 * Normal address modes.
		 */
		loc = db_read_address(loc, short_addr, regmodrm, &address);
		db_printf("%s", fp->f_name);
		switch(fp->f_size) {
		    case SNGL:
			db_printf("s");
			break;
		    case DBLR:
			db_printf("l");
			break;
		    case EXTR:
			db_printf("t");
			break;
		    case WORD:
			db_printf("s");
			break;
		    case LONG:
			db_printf("l");
			break;
		    case QUAD:
			db_printf("q");
			break;
		    default:
			break;
		}
		db_printf("\t");
		db_print_address(seg, BYTE, &address);
	} else {
		/*
		 * 'reg-reg' - special formats
		 */
		switch (fp->f_rrmode) {
		    case op2(ST,STI):
			name = (fp->f_rrname) ? fp->f_rrname : fp->f_name;
			db_printf("%s\t%%st,%%st(%d)",name,f_rm(regmodrm));
			break;
		    case op2(STI,ST):
			name = (fp->f_rrname) ? fp->f_rrname : fp->f_name;
			db_printf("%s\t%%st(%d),%%st",name, f_rm(regmodrm));
			break;
		    case op1(STI):
			name = (fp->f_rrname) ? fp->f_rrname : fp->f_name;
			db_printf("%s\t%%st(%d)",name, f_rm(regmodrm));
			break;
		    case op1(X):
			name = ((char * const *)fp->f_rrname)[f_rm(regmodrm)];
			if (*name == '\0')
				goto bad;
			db_printf("%s", name);
			break;
		    case op1(XA):
			name = ((char * const *)fp->f_rrname)[f_rm(regmodrm)];
			if (*name == '\0')
				goto bad;
			db_printf("%s\t%%ax", name);
			break;
		    default:
		    bad:
			db_printf("<bad instruction>");
			break;
		}
	}

	return (loc);
}

/*
 * Disassemble instruction at 'loc'.  'altfmt' specifies an
 * (optional) alternate format.  Return address of start of
 * next instruction.
 */
vaddr_t
db_disasm(vaddr_t loc, int altfmt)
{
	int	inst;
	int	size;
	int	short_addr;
	char *	seg;
	struct inst *	ip;
	char *	i_name;
	int	i_size;
	int	i_mode;
	int	regmodrm = 0;
	int	first;
	int	displ;
	int	prefix;
	int	imm;
	int	imm2;
	int	len;
	struct i_addr	address;
	char	tmpfmt[24];

	get_value_inc(inst, loc, 1, 0);
	short_addr = 0;
	size = LONG;
	seg = 0;

	/*
	 * Get prefixes
	 */
	prefix = 1;
	do {
		switch (inst) {
		    case 0x66:		/* data16 */
			size = WORD;
			break;
		    case 0x67:
			short_addr = 1;
			break;
		    case 0x26:
			seg = "%es";
			break;
		    case 0x36:
			seg = "%ss";
			break;
		    case 0x2e:
			seg = "%cs";
			break;
		    case 0x3e:
			seg = "%ds";
			break;
		    case 0x64:
			seg = "%fs";
			break;
		    case 0x65:
			seg = "%gs";
			break;
		    case 0xf0:
			db_printf("lock ");
			break;
		    case 0xf2:
			db_printf("repne ");
			break;
		    case 0xf3:
			db_printf("repe ");	/* XXX repe VS rep */
			break;
		    default:
			prefix = 0;
			break;
		}
		if (prefix)
			get_value_inc(inst, loc, 1, 0);
	} while (prefix);

	if (inst >= 0xd8 && inst <= 0xdf) {
		loc = db_disasm_esc(loc, inst, short_addr, size, seg);
		db_printf("\n");
		return (loc);
	}

	if (inst == 0x0f) {
		get_value_inc(inst, loc, 1, 0);
		ip = db_inst_0f[inst>>4];
		if (ip == 0)
			ip = &db_bad_inst;
		else
			ip = &ip[inst&0xf];
	} else {
		ip = &db_inst_table[inst];
	}

	if (ip->i_has_modrm) {
		get_value_inc(regmodrm, loc, 1, 0);
		loc = db_read_address(loc, short_addr, regmodrm, &address);
	}

	i_name = ip->i_name;
	i_size = ip->i_size;
	i_mode = ip->i_mode;

	if (i_size == RDEP) {
		/* sub-table to handle dependency on reg from ModR/M byte */
		ip = (struct inst *)ip->i_extra;
		ip = &ip[f_reg(regmodrm)];
		i_name = ip->i_name;
		i_mode = ip->i_mode;
		i_size = ip->i_size;
	} else if (i_name == NULL) {
		i_name = ((char **)ip->i_extra)[f_reg(regmodrm)];
	} else if (ip->i_extra == db_Grp3) {
		ip = (struct inst *)ip->i_extra;
		ip = &ip[f_reg(regmodrm)];
		i_name = ip->i_name;
		i_mode = ip->i_mode;
	}

	/* ModR/M-specific operation? */
	if ((i_mode & 0xFF) == MEx) {
		if (f_mod(regmodrm) != 3)
			i_mode = op1(E);
		else {
			/* unknown extension? */
			if (f_rm(regmodrm) > (i_mode >> 8))
				i_name = "";
			else {
				/* skip to the specific op */
				int i = f_rm(regmodrm);
				i_name = ip->i_extra;
				while (i-- > 0)
					while (*i_name++)
						;
			}
			i_mode = 0;
		}
	}

	if (i_size == SDEP) {
		if (size == WORD)
			db_printf("%s", i_name);
		else
			db_printf("%s", (char *)ip->i_extra);
	} else {
		db_printf("%s", i_name);
		if (i_size != NONE) {
			if (i_size == BYTE) {
				db_printf("b");
				size = BYTE;
			} else if (i_size == WORD) {
				db_printf("w");
				size = WORD;
			} else if (size == WORD) {
				db_printf("w");
			} else {
				db_printf("l");
			}
		}
	}
	db_printf("\t");
	for (first = 1;
	     i_mode != 0;
	     i_mode >>= 8, first = 0) {
		if (!first)
			db_printf(",");

		switch (i_mode & 0xFF) {
		    case E:
			db_print_address(seg, size, &address);
			break;
		    case Eind:
			db_printf("*");
			db_print_address(seg, size, &address);
			break;
		    case El:
			db_print_address(seg, LONG, &address);
			break;
		    case Ew:
			db_print_address(seg, WORD, &address);
			break;
		    case Eb:
			db_print_address(seg, BYTE, &address);
			break;
		    case R:
			db_printf("%s", db_reg[size][f_reg(regmodrm)]);
			break;
		    case Rw:
			db_printf("%s", db_reg[WORD][f_reg(regmodrm)]);
			break;
		    case Ri:
			db_printf("%s", db_reg[size][f_rm(inst)]);
			break;
		    case Ril:
			db_printf("%s", db_reg[LONG][f_rm(inst)]);
			break;
		    case S:
			db_printf("%s", db_seg_reg[f_reg(regmodrm)]);
			break;
		    case Si:
			db_printf("%s", db_seg_reg[f_reg(inst)]);
			break;
		    case A:
			db_printf("%s", db_reg[size][0]);	/* acc */
			break;
		    case BX:
			if (seg)
				db_printf("%s:", seg);
			db_printf("(%s)", short_addr ? "%bx" : "%ebx");
			break;
		    case CL:
			db_printf("%%cl");
			break;
		    case DX:
			db_printf("%%dx");
			break;
		    case SI:
			if (seg)
				db_printf("%s:", seg);
			db_printf("(%s)", short_addr ? "%si" : "%esi");
			break;
		    case DI:
			db_printf("%%es:(%s)", short_addr ? "%di" : "%edi");
			break;
		    case CR:
			db_printf("%%cr%d", f_reg(regmodrm));
			break;
		    case DR:
			db_printf("%%dr%d", f_reg(regmodrm));
			break;
		    case TR:
			db_printf("%%tr%d", f_reg(regmodrm));
			break;
		    case I:
			len = db_lengths[size];
			get_value_inc(imm, loc, len, 0);
			db_printf("$%s", db_format(tmpfmt, sizeof tmpfmt,
			    imm, DB_FORMAT_N, 1, 0));
			break;
		    case Is:
			len = db_lengths[size];
			get_value_inc(imm, loc, len, 1);
			db_printf("$%s", db_format(tmpfmt, sizeof tmpfmt,
			    imm, DB_FORMAT_R, 1, 0));
			break;
		    case Ib:
			get_value_inc(imm, loc, 1, 0);
			db_printf("$%s", db_format(tmpfmt, sizeof tmpfmt,
			    imm, DB_FORMAT_N, 1, 0));
			break;
		    case Iba:
			get_value_inc(imm, loc, 1, 0);
			if (imm != 0x0a)
				db_printf("$%s", db_format(tmpfmt, sizeof tmpfmt,
				    imm, DB_FORMAT_N, 1, 0));
			break;
		    case Ibs:
			get_value_inc(imm, loc, 1, 1);
			if (size == WORD)
				imm &= 0xFFFF;
			db_printf("$%s", db_format(tmpfmt, sizeof tmpfmt,
			    imm, DB_FORMAT_R, 1, 0));
			break;
		    case Iw:
			get_value_inc(imm, loc, 2, 0);
			db_printf("$%s", db_format(tmpfmt, sizeof tmpfmt,
			    imm, DB_FORMAT_N, 1, 0));
			break;
		    case O:
			if (short_addr)
				get_value_inc(displ, loc, 2, 1);
			else
				get_value_inc(displ, loc, 4, 1);
			if (seg)
				db_printf("%s:%s", seg, db_format(tmpfmt,
				    sizeof tmpfmt, displ, DB_FORMAT_R, 1, 0));
			else
				db_printsym((vaddr_t)displ, DB_STGY_ANY,
				    db_printf);
			break;
		    case Db:
			get_value_inc(displ, loc, 1, 1);
			displ += loc;
			if (size == WORD)
				displ &= 0xFFFF;
			db_printsym((vaddr_t)displ, DB_STGY_XTRN, db_printf);
			break;
		    case Dl:
			len = db_lengths[size];
			get_value_inc(displ, loc, len, 0);
			displ += loc;
			if (size == WORD)
				displ &= 0xFFFF;
			db_printsym((vaddr_t)displ, DB_STGY_XTRN, db_printf);
			break;
		    case o1:
			db_printf("$1");
			break;
		    case o3:
			db_printf("$3");
			break;
		    case OS:
			len = db_lengths[size];
			get_value_inc(imm, loc, len, 0);	/* offset */
			get_value_inc(imm2, loc, 2, 0);	/* segment */
			db_printf("$%s", db_format(tmpfmt, sizeof tmpfmt,
			    imm2, DB_FORMAT_N, 1, 0));
			db_printf(",%s", db_format(tmpfmt, sizeof tmpfmt,
			    imm, DB_FORMAT_N, 1, 0));
			break;
		}
	}

	if (altfmt == 0 && (inst == 0xe9 || inst == 0xeb)) {
		/*
		 * GAS pads to longword boundary after unconditional jumps.
		 */
		loc = (loc + (4-1)) & ~(4-1);
	}
	db_printf("\n");
	return (loc);
}
