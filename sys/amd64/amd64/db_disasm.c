/*-
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
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
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
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Instruction disassembler.
 */
#include <sys/param.h>
#include <sys/libkern.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>

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
#define	ADEP	8
#define	ESC	9
#define	NONE	10

/*
 * REX prefix and bits
 */
#define REX_B	1
#define REX_X	2
#define REX_R	4
#define REX_W	8
#define REX	0x40

/*
 * Addressing modes
 */
#define	E	1			/* general effective address */
#define	Eind	2			/* indirect address (jump, call) */
#define	Ew	3			/* address, word size */
#define	Eb	4			/* address, byte size */
#define	R	5			/* register, in 'reg' field */
#define	Rw	6			/* word register, in 'reg' field */
#define	Rq	39			/* quad register, in 'reg' field */
#define	Rv	40			/* register in 'r/m' field */
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
#define	Ilq	24			/* long/quad immediate, unsigned */
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
#define	El	35			/* address, long/quad size */
#define	Ril	36			/* long register in instruction */
#define	Iba	37			/* byte immediate, don't print if 0xa */
#define	EL	38			/* address, explicitly long size */

struct inst {
	const char *	i_name;		/* name */
	short	i_has_modrm;		/* has regmodrm byte */
	short	i_size;			/* operand size */
	int	i_mode;			/* addressing modes */
	const void *	i_extra;	/* pointer to extra opcode table */
};

#define	op1(x)		(x)
#define	op2(x,y)	((x)|((y)<<8))
#define	op3(x,y,z)	((x)|((y)<<8)|((z)<<16))

struct finst {
	const char *	f_name;		/* name for memory instruction */
	int	f_size;			/* size for memory instruction */
	int	f_rrmode;		/* mode for rr instruction */
	const void *	f_rrname;	/* name for rr instruction
					   (or pointer to table) */
};

static const struct inst db_inst_0f388x[] = {
/*80*/	{ "",	   TRUE,  SDEP,  op2(E, Rq),  "invept" },
/*81*/	{ "",	   TRUE,  SDEP,  op2(E, Rq),  "invvpid" },
/*82*/	{ "",	   TRUE,  SDEP,  op2(E, Rq),  "invpcid" },
/*83*/	{ "",	   FALSE, NONE,  0,	      0 },
/*84*/	{ "",	   FALSE, NONE,  0,	      0 },
/*85*/	{ "",	   FALSE, NONE,  0,	      0 },
/*86*/	{ "",	   FALSE, NONE,  0,	      0 },
/*87*/	{ "",	   FALSE, NONE,  0,	      0 },

/*88*/	{ "",	   FALSE, NONE,  0,	      0 },
/*89*/	{ "",	   FALSE, NONE,  0,	      0 },
/*8a*/	{ "",	   FALSE, NONE,  0,	      0 },
/*8b*/	{ "",	   FALSE, NONE,  0,	      0 },
/*8c*/	{ "",	   FALSE, NONE,  0,	      0 },
/*8d*/	{ "",	   FALSE, NONE,  0,	      0 },
/*8e*/	{ "",	   FALSE, NONE,  0,	      0 },
/*8f*/	{ "",	   FALSE, NONE,  0,	      0 },
};

static const struct inst * const db_inst_0f38[] = {
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	db_inst_0f388x,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

static const char * const db_Grp6[] = {
	"sldt",
	"str",
	"lldt",
	"ltr",
	"verr",
	"verw",
	"",
	""
};

static const char * const db_Grp7[] = {
	"sgdt",
	"sidt",
	"lgdt",
	"lidt",
	"smsw",
	"",
	"lmsw",
	"invlpg"
};

static const char * const db_Grp8[] = {
	"",
	"",
	"",
	"",
	"bt",
	"bts",
	"btr",
	"btc"
};

static const char * const db_Grp9[] = {
	"",
	"cmpxchg8b",
	"",
	"",
	"",
	"",
	"vmptrld",
	"vmptrst"
};

static const char * const db_Grp15[] = {
	"fxsave",
	"fxrstor",
	"ldmxcsr",
	"stmxcsr",
	"xsave",
	"xrstor",
	"xsaveopt",
	"clflush"
};

static const char * const db_Grp15b[] = {
	"",
	"",
	"",
	"",
	"",
	"lfence",
	"mfence",
	"sfence"
};

static const struct inst db_inst_0f0x[] = {
/*00*/	{ "",	   TRUE,  NONE,  op1(Ew),     db_Grp6 },
/*01*/	{ "",	   TRUE,  NONE,  op1(Ew),     db_Grp7 },
/*02*/	{ "lar",   TRUE,  LONG,  op2(E,R),    0 },
/*03*/	{ "lsl",   TRUE,  LONG,  op2(E,R),    0 },
/*04*/	{ "",      FALSE, NONE,  0,	      0 },
/*05*/	{ "syscall",FALSE,NONE,  0,	      0 },
/*06*/	{ "clts",  FALSE, NONE,  0,	      0 },
/*07*/	{ "sysret",FALSE, NONE,  0,	      0 },

/*08*/	{ "invd",  FALSE, NONE,  0,	      0 },
/*09*/	{ "wbinvd",FALSE, NONE,  0,	      0 },
/*0a*/	{ "",      FALSE, NONE,  0,	      0 },
/*0b*/	{ "",      FALSE, NONE,  0,	      0 },
/*0c*/	{ "",      FALSE, NONE,  0,	      0 },
/*0d*/	{ "",      FALSE, NONE,  0,	      0 },
/*0e*/	{ "",      FALSE, NONE,  0,	      0 },
/*0f*/	{ "",      FALSE, NONE,  0,	      0 },
};

static const struct inst db_inst_0f1x[] = {
/*10*/	{ "",      FALSE, NONE,  0,	      0 },
/*11*/	{ "",      FALSE, NONE,  0,	      0 },
/*12*/	{ "",      FALSE, NONE,  0,	      0 },
/*13*/	{ "",      FALSE, NONE,  0,	      0 },
/*14*/	{ "",      FALSE, NONE,  0,	      0 },
/*15*/	{ "",      FALSE, NONE,  0,	      0 },
/*16*/	{ "",      FALSE, NONE,  0,	      0 },
/*17*/	{ "",      FALSE, NONE,  0,	      0 },

/*18*/	{ "",      FALSE, NONE,  0,	      0 },
/*19*/	{ "",      FALSE, NONE,  0,	      0 },
/*1a*/	{ "",      FALSE, NONE,  0,	      0 },
/*1b*/	{ "",      FALSE, NONE,  0,	      0 },
/*1c*/	{ "",      FALSE, NONE,  0,	      0 },
/*1d*/	{ "",      FALSE, NONE,  0,	      0 },
/*1e*/	{ "",      FALSE, NONE,  0,	      0 },
/*1f*/	{ "nopl",  TRUE,  SDEP,  0,	      "nopw" },
};

static const struct inst db_inst_0f2x[] = {
/*20*/	{ "mov",   TRUE,  LONG,  op2(CR,El),  0 },
/*21*/	{ "mov",   TRUE,  LONG,  op2(DR,El),  0 },
/*22*/	{ "mov",   TRUE,  LONG,  op2(El,CR),  0 },
/*23*/	{ "mov",   TRUE,  LONG,  op2(El,DR),  0 },
/*24*/	{ "mov",   TRUE,  LONG,  op2(TR,El),  0 },
/*25*/	{ "",      FALSE, NONE,  0,	      0 },
/*26*/	{ "mov",   TRUE,  LONG,  op2(El,TR),  0 },
/*27*/	{ "",      FALSE, NONE,  0,	      0 },

/*28*/	{ "",      FALSE, NONE,  0,	      0 },
/*29*/	{ "",      FALSE, NONE,  0,	      0 },
/*2a*/	{ "",      FALSE, NONE,  0,	      0 },
/*2b*/	{ "",      FALSE, NONE,  0,	      0 },
/*2c*/	{ "",      FALSE, NONE,  0,	      0 },
/*2d*/	{ "",      FALSE, NONE,  0,	      0 },
/*2e*/	{ "",      FALSE, NONE,  0,	      0 },
/*2f*/	{ "",      FALSE, NONE,  0,	      0 },
};

static const struct inst db_inst_0f3x[] = {
/*30*/	{ "wrmsr", FALSE, NONE,  0,	      0 },
/*31*/	{ "rdtsc", FALSE, NONE,  0,	      0 },
/*32*/	{ "rdmsr", FALSE, NONE,  0,	      0 },
/*33*/	{ "rdpmc", FALSE, NONE,  0,	      0 },
/*34*/	{ "sysenter",FALSE,NONE,  0,	      0 },
/*35*/	{ "sysexit",FALSE,NONE,  0,	      0 },
/*36*/	{ "",	   FALSE, NONE,  0,	      0 },
/*37*/	{ "getsec",FALSE, NONE,  0,	      0 },

/*38*/	{ "",	   FALSE, ESC,  0,	      db_inst_0f38 },
/*39*/	{ "",	   FALSE, NONE,  0,	      0 },
/*3a*/	{ "",	   FALSE, NONE,  0,	      0 },
/*3b*/	{ "",	   FALSE, NONE,  0,	      0 },
/*3c*/	{ "",	   FALSE, NONE,  0,	      0 },
/*3d*/	{ "",	   FALSE, NONE,  0,	      0 },
/*3e*/	{ "",	   FALSE, NONE,  0,	      0 },
/*3f*/	{ "",	   FALSE, NONE,  0,	      0 },
};

static const struct inst db_inst_0f4x[] = {
/*40*/	{ "cmovo",  TRUE, NONE,  op2(E, R),   0 },
/*41*/	{ "cmovno", TRUE, NONE,  op2(E, R),   0 },
/*42*/	{ "cmovb",  TRUE, NONE,  op2(E, R),   0 },
/*43*/	{ "cmovnb", TRUE, NONE,  op2(E, R),   0 },
/*44*/	{ "cmovz",  TRUE, NONE,  op2(E, R),   0 },
/*45*/	{ "cmovnz", TRUE, NONE,  op2(E, R),   0 },
/*46*/	{ "cmovbe", TRUE, NONE,  op2(E, R),   0 },
/*47*/	{ "cmovnbe",TRUE, NONE,  op2(E, R),   0 },

/*48*/	{ "cmovs",  TRUE, NONE,  op2(E, R),   0 },
/*49*/	{ "cmovns", TRUE, NONE,  op2(E, R),   0 },
/*4a*/	{ "cmovp",  TRUE, NONE,  op2(E, R),   0 },
/*4b*/	{ "cmovnp", TRUE, NONE,  op2(E, R),   0 },
/*4c*/	{ "cmovl",  TRUE, NONE,  op2(E, R),   0 },
/*4d*/	{ "cmovnl", TRUE, NONE,  op2(E, R),   0 },
/*4e*/	{ "cmovle", TRUE, NONE,  op2(E, R),   0 },
/*4f*/	{ "cmovnle",TRUE, NONE,  op2(E, R),   0 },
};

static const struct inst db_inst_0f7x[] = {
/*70*/	{ "",	   FALSE, NONE,  0,	      0 },
/*71*/	{ "",	   FALSE, NONE,  0,	      0 },
/*72*/	{ "",	   FALSE, NONE,  0,	      0 },
/*73*/	{ "",	   FALSE, NONE,  0,	      0 },
/*74*/	{ "",	   FALSE, NONE,  0,	      0 },
/*75*/	{ "",	   FALSE, NONE,  0,	      0 },
/*76*/	{ "",	   FALSE, NONE,  0,	      0 },
/*77*/	{ "",	   FALSE, NONE,  0,	      0 },

/*78*/	{ "vmread", TRUE, NONE,  op2(Rq, E),  0 },
/*79*/	{ "vmwrite",TRUE, NONE,  op2(E, Rq),  0 },
/*7a*/	{ "",	   FALSE, NONE,  0,	      0 },
/*7b*/	{ "",	   FALSE, NONE,  0,	      0 },
/*7c*/	{ "",	   FALSE, NONE,  0,	      0 },
/*7d*/	{ "",	   FALSE, NONE,  0,	      0 },
/*7e*/	{ "",	   FALSE, NONE,  0,	      0 },
/*7f*/	{ "",	   FALSE, NONE,  0,	      0 },
};

static const struct inst db_inst_0f8x[] = {
/*80*/	{ "jo",    FALSE, NONE,  op1(Dl),     0 },
/*81*/	{ "jno",   FALSE, NONE,  op1(Dl),     0 },
/*82*/	{ "jb",    FALSE, NONE,  op1(Dl),     0 },
/*83*/	{ "jnb",   FALSE, NONE,  op1(Dl),     0 },
/*84*/	{ "jz",    FALSE, NONE,  op1(Dl),     0 },
/*85*/	{ "jnz",   FALSE, NONE,  op1(Dl),     0 },
/*86*/	{ "jbe",   FALSE, NONE,  op1(Dl),     0 },
/*87*/	{ "jnbe",  FALSE, NONE,  op1(Dl),     0 },

/*88*/	{ "js",    FALSE, NONE,  op1(Dl),     0 },
/*89*/	{ "jns",   FALSE, NONE,  op1(Dl),     0 },
/*8a*/	{ "jp",    FALSE, NONE,  op1(Dl),     0 },
/*8b*/	{ "jnp",   FALSE, NONE,  op1(Dl),     0 },
/*8c*/	{ "jl",    FALSE, NONE,  op1(Dl),     0 },
/*8d*/	{ "jnl",   FALSE, NONE,  op1(Dl),     0 },
/*8e*/	{ "jle",   FALSE, NONE,  op1(Dl),     0 },
/*8f*/	{ "jnle",  FALSE, NONE,  op1(Dl),     0 },
};

static const struct inst db_inst_0f9x[] = {
/*90*/	{ "seto",  TRUE,  NONE,  op1(Eb),     0 },
/*91*/	{ "setno", TRUE,  NONE,  op1(Eb),     0 },
/*92*/	{ "setb",  TRUE,  NONE,  op1(Eb),     0 },
/*93*/	{ "setnb", TRUE,  NONE,  op1(Eb),     0 },
/*94*/	{ "setz",  TRUE,  NONE,  op1(Eb),     0 },
/*95*/	{ "setnz", TRUE,  NONE,  op1(Eb),     0 },
/*96*/	{ "setbe", TRUE,  NONE,  op1(Eb),     0 },
/*97*/	{ "setnbe",TRUE,  NONE,  op1(Eb),     0 },

/*98*/	{ "sets",  TRUE,  NONE,  op1(Eb),     0 },
/*99*/	{ "setns", TRUE,  NONE,  op1(Eb),     0 },
/*9a*/	{ "setp",  TRUE,  NONE,  op1(Eb),     0 },
/*9b*/	{ "setnp", TRUE,  NONE,  op1(Eb),     0 },
/*9c*/	{ "setl",  TRUE,  NONE,  op1(Eb),     0 },
/*9d*/	{ "setnl", TRUE,  NONE,  op1(Eb),     0 },
/*9e*/	{ "setle", TRUE,  NONE,  op1(Eb),     0 },
/*9f*/	{ "setnle",TRUE,  NONE,  op1(Eb),     0 },
};

static const struct inst db_inst_0fax[] = {
/*a0*/	{ "push",  FALSE, NONE,  op1(Si),     0 },
/*a1*/	{ "pop",   FALSE, NONE,  op1(Si),     0 },
/*a2*/	{ "cpuid", FALSE, NONE,  0,	      0 },
/*a3*/	{ "bt",    TRUE,  LONG,  op2(R,E),    0 },
/*a4*/	{ "shld",  TRUE,  LONG,  op3(Ib,R,E), 0 },
/*a5*/	{ "shld",  TRUE,  LONG,  op3(CL,R,E), 0 },
/*a6*/	{ "",      FALSE, NONE,  0,	      0 },
/*a7*/	{ "",      FALSE, NONE,  0,	      0 },

/*a8*/	{ "push",  FALSE, NONE,  op1(Si),     0 },
/*a9*/	{ "pop",   FALSE, NONE,  op1(Si),     0 },
/*aa*/	{ "rsm",   FALSE, NONE,  0,	      0 },
/*ab*/	{ "bts",   TRUE,  LONG,  op2(R,E),    0 },
/*ac*/	{ "shrd",  TRUE,  LONG,  op3(Ib,R,E), 0 },
/*ad*/	{ "shrd",  TRUE,  LONG,  op3(CL,R,E), 0 },
/*ae*/	{ "",      TRUE,  LONG,  op1(E),      db_Grp15 },
/*af*/	{ "imul",  TRUE,  LONG,  op2(E,R),    0 },
};

static const struct inst db_inst_0fbx[] = {
/*b0*/	{ "cmpxchg",TRUE, BYTE,	 op2(R, E),   0 },
/*b0*/	{ "cmpxchg",TRUE, LONG,	 op2(R, E),   0 },
/*b2*/	{ "lss",   TRUE,  LONG,  op2(E, R),   0 },
/*b3*/	{ "btr",   TRUE,  LONG,  op2(R, E),   0 },
/*b4*/	{ "lfs",   TRUE,  LONG,  op2(E, R),   0 },
/*b5*/	{ "lgs",   TRUE,  LONG,  op2(E, R),   0 },
/*b6*/	{ "movzb", TRUE,  LONG,  op2(Eb, R),  0 },
/*b7*/	{ "movzw", TRUE,  LONG,  op2(Ew, R),  0 },

/*b8*/	{ "",      FALSE, NONE,  0,	      0 },
/*b9*/	{ "",      FALSE, NONE,  0,	      0 },
/*ba*/	{ "",      TRUE,  LONG,  op2(Ib, E),  db_Grp8 },
/*bb*/	{ "btc",   TRUE,  LONG,  op2(R, E),   0 },
/*bc*/	{ "bsf",   TRUE,  LONG,  op2(E, R),   0 },
/*bd*/	{ "bsr",   TRUE,  LONG,  op2(E, R),   0 },
/*be*/	{ "movsb", TRUE,  LONG,  op2(Eb, R),  0 },
/*bf*/	{ "movsw", TRUE,  LONG,  op2(Ew, R),  0 },
};

static const struct inst db_inst_0fcx[] = {
/*c0*/	{ "xadd",  TRUE,  BYTE,	 op2(R, E),   0 },
/*c1*/	{ "xadd",  TRUE,  LONG,	 op2(R, E),   0 },
/*c2*/	{ "",	   FALSE, NONE,	 0,	      0 },
/*c3*/	{ "",	   FALSE, NONE,	 0,	      0 },
/*c4*/	{ "",	   FALSE, NONE,	 0,	      0 },
/*c5*/	{ "",	   FALSE, NONE,	 0,	      0 },
/*c6*/	{ "",	   FALSE, NONE,	 0,	      0 },
/*c7*/	{ "",	   TRUE,  NONE,  op1(E),      db_Grp9 },
/*c8*/	{ "bswap", FALSE, LONG,  op1(Ril),    0 },
/*c9*/	{ "bswap", FALSE, LONG,  op1(Ril),    0 },
/*ca*/	{ "bswap", FALSE, LONG,  op1(Ril),    0 },
/*cb*/	{ "bswap", FALSE, LONG,  op1(Ril),    0 },
/*cc*/	{ "bswap", FALSE, LONG,  op1(Ril),    0 },
/*cd*/	{ "bswap", FALSE, LONG,  op1(Ril),    0 },
/*ce*/	{ "bswap", FALSE, LONG,  op1(Ril),    0 },
/*cf*/	{ "bswap", FALSE, LONG,  op1(Ril),    0 },
};

static const struct inst * const db_inst_0f[] = {
	db_inst_0f0x,
	db_inst_0f1x,
	db_inst_0f2x,
	db_inst_0f3x,
	db_inst_0f4x,
	0,
	0,
	db_inst_0f7x,
	db_inst_0f8x,
	db_inst_0f9x,
	db_inst_0fax,
	db_inst_0fbx,
	db_inst_0fcx,
	0,
	0,
	0
};

static const char * const db_Esc92[] = {
	"fnop",	"",	"",	"",	"",	"",	"",	""
};
static const char * const db_Esc94[] = {
	"fchs",	"fabs",	"",	"",	"ftst",	"fxam",	"",	""
};
static const char * const db_Esc95[] = {
	"fld1",	"fldl2t","fldl2e","fldpi","fldlg2","fldln2","fldz",""
};
static const char * const db_Esc96[] = {
	"f2xm1","fyl2x","fptan","fpatan","fxtract","fprem1","fdecstp",
	"fincstp"
};
static const char * const db_Esc97[] = {
	"fprem","fyl2xp1","fsqrt","fsincos","frndint","fscale","fsin","fcos"
};

static const char * const db_Esca5[] = {
	"",	"fucompp","",	"",	"",	"",	"",	""
};

static const char * const db_Escb4[] = {
	"fneni","fndisi",	"fnclex","fninit","fsetpm",	"",	"",	""
};

static const char * const db_Esce3[] = {
	"",	"fcompp","",	"",	"",	"",	"",	""
};

static const char * const db_Escf4[] = {
	"fnstsw","",	"",	"",	"",	"",	"",	""
};

static const struct finst db_Esc8[] = {
/*0*/	{ "fadd",   SNGL,  op2(STI,ST),	0 },
/*1*/	{ "fmul",   SNGL,  op2(STI,ST),	0 },
/*2*/	{ "fcom",   SNGL,  op2(STI,ST),	0 },
/*3*/	{ "fcomp",  SNGL,  op2(STI,ST),	0 },
/*4*/	{ "fsub",   SNGL,  op2(STI,ST),	0 },
/*5*/	{ "fsubr",  SNGL,  op2(STI,ST),	0 },
/*6*/	{ "fdiv",   SNGL,  op2(STI,ST),	0 },
/*7*/	{ "fdivr",  SNGL,  op2(STI,ST),	0 },
};

static const struct finst db_Esc9[] = {
/*0*/	{ "fld",    SNGL,  op1(STI),	0 },
/*1*/	{ "",       NONE,  op1(STI),	"fxch" },
/*2*/	{ "fst",    SNGL,  op1(X),	db_Esc92 },
/*3*/	{ "fstp",   SNGL,  0,		0 },
/*4*/	{ "fldenv", NONE,  op1(X),	db_Esc94 },
/*5*/	{ "fldcw",  NONE,  op1(X),	db_Esc95 },
/*6*/	{ "fnstenv",NONE,  op1(X),	db_Esc96 },
/*7*/	{ "fnstcw", NONE,  op1(X),	db_Esc97 },
};

static const struct finst db_Esca[] = {
/*0*/	{ "fiadd",  LONG,  0,		0 },
/*1*/	{ "fimul",  LONG,  0,		0 },
/*2*/	{ "ficom",  LONG,  0,		0 },
/*3*/	{ "ficomp", LONG,  0,		0 },
/*4*/	{ "fisub",  LONG,  0,		0 },
/*5*/	{ "fisubr", LONG,  op1(X),	db_Esca5 },
/*6*/	{ "fidiv",  LONG,  0,		0 },
/*7*/	{ "fidivr", LONG,  0,		0 }
};

static const struct finst db_Escb[] = {
/*0*/	{ "fild",   LONG,  0,		0 },
/*1*/	{ "",       NONE,  0,		0 },
/*2*/	{ "fist",   LONG,  0,		0 },
/*3*/	{ "fistp",  LONG,  0,		0 },
/*4*/	{ "",       WORD,  op1(X),	db_Escb4 },
/*5*/	{ "fld",    EXTR,  0,		0 },
/*6*/	{ "",       WORD,  0,		0 },
/*7*/	{ "fstp",   EXTR,  0,		0 },
};

static const struct finst db_Escc[] = {
/*0*/	{ "fadd",   DBLR,  op2(ST,STI),	0 },
/*1*/	{ "fmul",   DBLR,  op2(ST,STI),	0 },
/*2*/	{ "fcom",   DBLR,  0,		0 },
/*3*/	{ "fcomp",  DBLR,  0,		0 },
/*4*/	{ "fsub",   DBLR,  op2(ST,STI),	"fsubr" },
/*5*/	{ "fsubr",  DBLR,  op2(ST,STI),	"fsub" },
/*6*/	{ "fdiv",   DBLR,  op2(ST,STI),	"fdivr" },
/*7*/	{ "fdivr",  DBLR,  op2(ST,STI),	"fdiv" },
};

static const struct finst db_Escd[] = {
/*0*/	{ "fld",    DBLR,  op1(STI),	"ffree" },
/*1*/	{ "",       NONE,  0,		0 },
/*2*/	{ "fst",    DBLR,  op1(STI),	0 },
/*3*/	{ "fstp",   DBLR,  op1(STI),	0 },
/*4*/	{ "frstor", NONE,  op1(STI),	"fucom" },
/*5*/	{ "",       NONE,  op1(STI),	"fucomp" },
/*6*/	{ "fnsave", NONE,  0,		0 },
/*7*/	{ "fnstsw", NONE,  0,		0 },
};

static const struct finst db_Esce[] = {
/*0*/	{ "fiadd",  WORD,  op2(ST,STI),	"faddp" },
/*1*/	{ "fimul",  WORD,  op2(ST,STI),	"fmulp" },
/*2*/	{ "ficom",  WORD,  0,		0 },
/*3*/	{ "ficomp", WORD,  op1(X),	db_Esce3 },
/*4*/	{ "fisub",  WORD,  op2(ST,STI),	"fsubrp" },
/*5*/	{ "fisubr", WORD,  op2(ST,STI),	"fsubp" },
/*6*/	{ "fidiv",  WORD,  op2(ST,STI),	"fdivrp" },
/*7*/	{ "fidivr", WORD,  op2(ST,STI),	"fdivp" },
};

static const struct finst db_Escf[] = {
/*0*/	{ "fild",   WORD,  0,		0 },
/*1*/	{ "",       NONE,  0,		0 },
/*2*/	{ "fist",   WORD,  0,		0 },
/*3*/	{ "fistp",  WORD,  0,		0 },
/*4*/	{ "fbld",   NONE,  op1(XA),	db_Escf4 },
/*5*/	{ "fild",   QUAD,  0,		0 },
/*6*/	{ "fbstp",  NONE,  0,		0 },
/*7*/	{ "fistp",  QUAD,  0,		0 },
};

static const struct finst * const db_Esc_inst[] = {
	db_Esc8, db_Esc9, db_Esca, db_Escb,
	db_Escc, db_Escd, db_Esce, db_Escf
};

static const char * const db_Grp1[] = {
	"add",
	"or",
	"adc",
	"sbb",
	"and",
	"sub",
	"xor",
	"cmp"
};

static const char * const db_Grp2[] = {
	"rol",
	"ror",
	"rcl",
	"rcr",
	"shl",
	"shr",
	"shl",
	"sar"
};

static const struct inst db_Grp3[] = {
	{ "test",  TRUE, NONE, op2(I,E), 0 },
	{ "test",  TRUE, NONE, op2(I,E), 0 },
	{ "not",   TRUE, NONE, op1(E),   0 },
	{ "neg",   TRUE, NONE, op1(E),   0 },
	{ "mul",   TRUE, NONE, op2(E,A), 0 },
	{ "imul",  TRUE, NONE, op2(E,A), 0 },
	{ "div",   TRUE, NONE, op2(E,A), 0 },
	{ "idiv",  TRUE, NONE, op2(E,A), 0 },
};

static const struct inst db_Grp4[] = {
	{ "inc",   TRUE, BYTE, op1(E),   0 },
	{ "dec",   TRUE, BYTE, op1(E),   0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 }
};

static const struct inst db_Grp5[] = {
	{ "inc",   TRUE, LONG, op1(E),   0 },
	{ "dec",   TRUE, LONG, op1(E),   0 },
	{ "call",  TRUE, LONG, op1(Eind),0 },
	{ "lcall", TRUE, LONG, op1(Eind),0 },
	{ "jmp",   TRUE, LONG, op1(Eind),0 },
	{ "ljmp",  TRUE, LONG, op1(Eind),0 },
	{ "push",  TRUE, LONG, op1(E),   0 },
	{ "",      TRUE, NONE, 0,	 0 }
};

static const struct inst db_Grp9b[] = {
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "",      TRUE, NONE, 0,	 0 },
	{ "rdrand",TRUE, LONG, op1(Rv),  0 },
	{ "rdseed",TRUE, LONG, op1(Rv),  0 }
};

static const struct inst db_inst_table[256] = {
/*00*/	{ "add",   TRUE,  BYTE,  op2(R, E),  0 },
/*01*/	{ "add",   TRUE,  LONG,  op2(R, E),  0 },
/*02*/	{ "add",   TRUE,  BYTE,  op2(E, R),  0 },
/*03*/	{ "add",   TRUE,  LONG,  op2(E, R),  0 },
/*04*/	{ "add",   FALSE, BYTE,  op2(I, A),  0 },
/*05*/	{ "add",   FALSE, LONG,  op2(Is, A), 0 },
/*06*/	{ "push",  FALSE, NONE,  op1(Si),    0 },
/*07*/	{ "pop",   FALSE, NONE,  op1(Si),    0 },

/*08*/	{ "or",    TRUE,  BYTE,  op2(R, E),  0 },
/*09*/	{ "or",    TRUE,  LONG,  op2(R, E),  0 },
/*0a*/	{ "or",    TRUE,  BYTE,  op2(E, R),  0 },
/*0b*/	{ "or",    TRUE,  LONG,  op2(E, R),  0 },
/*0c*/	{ "or",    FALSE, BYTE,  op2(I, A),  0 },
/*0d*/	{ "or",    FALSE, LONG,  op2(I, A),  0 },
/*0e*/	{ "push",  FALSE, NONE,  op1(Si),    0 },
/*0f*/	{ "",      FALSE, ESC,   0,	     db_inst_0f },

/*10*/	{ "adc",   TRUE,  BYTE,  op2(R, E),  0 },
/*11*/	{ "adc",   TRUE,  LONG,  op2(R, E),  0 },
/*12*/	{ "adc",   TRUE,  BYTE,  op2(E, R),  0 },
/*13*/	{ "adc",   TRUE,  LONG,  op2(E, R),  0 },
/*14*/	{ "adc",   FALSE, BYTE,  op2(I, A),  0 },
/*15*/	{ "adc",   FALSE, LONG,  op2(Is, A), 0 },
/*16*/	{ "push",  FALSE, NONE,  op1(Si),    0 },
/*17*/	{ "pop",   FALSE, NONE,  op1(Si),    0 },

/*18*/	{ "sbb",   TRUE,  BYTE,  op2(R, E),  0 },
/*19*/	{ "sbb",   TRUE,  LONG,  op2(R, E),  0 },
/*1a*/	{ "sbb",   TRUE,  BYTE,  op2(E, R),  0 },
/*1b*/	{ "sbb",   TRUE,  LONG,  op2(E, R),  0 },
/*1c*/	{ "sbb",   FALSE, BYTE,  op2(I, A),  0 },
/*1d*/	{ "sbb",   FALSE, LONG,  op2(Is, A), 0 },
/*1e*/	{ "push",  FALSE, NONE,  op1(Si),    0 },
/*1f*/	{ "pop",   FALSE, NONE,  op1(Si),    0 },

/*20*/	{ "and",   TRUE,  BYTE,  op2(R, E),  0 },
/*21*/	{ "and",   TRUE,  LONG,  op2(R, E),  0 },
/*22*/	{ "and",   TRUE,  BYTE,  op2(E, R),  0 },
/*23*/	{ "and",   TRUE,  LONG,  op2(E, R),  0 },
/*24*/	{ "and",   FALSE, BYTE,  op2(I, A),  0 },
/*25*/	{ "and",   FALSE, LONG,  op2(I, A),  0 },
/*26*/	{ "",      FALSE, NONE,  0,	     0 },
/*27*/	{ "daa",   FALSE, NONE,  0,	     0 },

/*28*/	{ "sub",   TRUE,  BYTE,  op2(R, E),  0 },
/*29*/	{ "sub",   TRUE,  LONG,  op2(R, E),  0 },
/*2a*/	{ "sub",   TRUE,  BYTE,  op2(E, R),  0 },
/*2b*/	{ "sub",   TRUE,  LONG,  op2(E, R),  0 },
/*2c*/	{ "sub",   FALSE, BYTE,  op2(I, A),  0 },
/*2d*/	{ "sub",   FALSE, LONG,  op2(Is, A), 0 },
/*2e*/	{ "",      FALSE, NONE,  0,	     0 },
/*2f*/	{ "das",   FALSE, NONE,  0,	     0 },

/*30*/	{ "xor",   TRUE,  BYTE,  op2(R, E),  0 },
/*31*/	{ "xor",   TRUE,  LONG,  op2(R, E),  0 },
/*32*/	{ "xor",   TRUE,  BYTE,  op2(E, R),  0 },
/*33*/	{ "xor",   TRUE,  LONG,  op2(E, R),  0 },
/*34*/	{ "xor",   FALSE, BYTE,  op2(I, A),  0 },
/*35*/	{ "xor",   FALSE, LONG,  op2(I, A),  0 },
/*36*/	{ "",      FALSE, NONE,  0,	     0 },
/*37*/	{ "aaa",   FALSE, NONE,  0,	     0 },

/*38*/	{ "cmp",   TRUE,  BYTE,  op2(R, E),  0 },
/*39*/	{ "cmp",   TRUE,  LONG,  op2(R, E),  0 },
/*3a*/	{ "cmp",   TRUE,  BYTE,  op2(E, R),  0 },
/*3b*/	{ "cmp",   TRUE,  LONG,  op2(E, R),  0 },
/*3c*/	{ "cmp",   FALSE, BYTE,  op2(I, A),  0 },
/*3d*/	{ "cmp",   FALSE, LONG,  op2(Is, A), 0 },
/*3e*/	{ "",      FALSE, NONE,  0,	     0 },
/*3f*/	{ "aas",   FALSE, NONE,  0,	     0 },

/*40*/	{ "rex",   FALSE, NONE,  0,          0 },
/*41*/	{ "rex.b", FALSE, NONE,  0,          0 },
/*42*/	{ "rex.x", FALSE, NONE,  0,          0 },
/*43*/	{ "rex.xb", FALSE, NONE, 0,          0 },
/*44*/	{ "rex.r", FALSE, NONE,  0,          0 },
/*45*/	{ "rex.rb", FALSE, NONE, 0,          0 },
/*46*/	{ "rex.rx", FALSE, NONE, 0,          0 },
/*47*/	{ "rex.rxb", FALSE, NONE, 0,         0 },

/*48*/	{ "rex.w", FALSE, NONE,  0,          0 },
/*49*/	{ "rex.wb", FALSE, NONE, 0,          0 },
/*4a*/	{ "rex.wx", FALSE, NONE, 0,          0 },
/*4b*/	{ "rex.wxb", FALSE, NONE, 0,         0 },
/*4c*/	{ "rex.wr", FALSE, NONE, 0,          0 },
/*4d*/	{ "rex.wrb", FALSE, NONE, 0,         0 },
/*4e*/	{ "rex.wrx", FALSE, NONE, 0,         0 },
/*4f*/	{ "rex.wrxb", FALSE, NONE, 0,        0 },

/*50*/	{ "push",  FALSE, LONG,  op1(Ri),    0 },
/*51*/	{ "push",  FALSE, LONG,  op1(Ri),    0 },
/*52*/	{ "push",  FALSE, LONG,  op1(Ri),    0 },
/*53*/	{ "push",  FALSE, LONG,  op1(Ri),    0 },
/*54*/	{ "push",  FALSE, LONG,  op1(Ri),    0 },
/*55*/	{ "push",  FALSE, LONG,  op1(Ri),    0 },
/*56*/	{ "push",  FALSE, LONG,  op1(Ri),    0 },
/*57*/	{ "push",  FALSE, LONG,  op1(Ri),    0 },

/*58*/	{ "pop",   FALSE, LONG,  op1(Ri),    0 },
/*59*/	{ "pop",   FALSE, LONG,  op1(Ri),    0 },
/*5a*/	{ "pop",   FALSE, LONG,  op1(Ri),    0 },
/*5b*/	{ "pop",   FALSE, LONG,  op1(Ri),    0 },
/*5c*/	{ "pop",   FALSE, LONG,  op1(Ri),    0 },
/*5d*/	{ "pop",   FALSE, LONG,  op1(Ri),    0 },
/*5e*/	{ "pop",   FALSE, LONG,  op1(Ri),    0 },
/*5f*/	{ "pop",   FALSE, LONG,  op1(Ri),    0 },

/*60*/	{ "pusha", FALSE, LONG,  0,	     0 },
/*61*/	{ "popa",  FALSE, LONG,  0,	     0 },
/*62*/  { "bound", TRUE,  LONG,  op2(E, R),  0 },
/*63*/	{ "movslq",  TRUE,  NONE,  op2(EL,R), 0 },

/*64*/	{ "",      FALSE, NONE,  0,	     0 },
/*65*/	{ "",      FALSE, NONE,  0,	     0 },
/*66*/	{ "",      FALSE, NONE,  0,	     0 },
/*67*/	{ "",      FALSE, NONE,  0,	     0 },

/*68*/	{ "push",  FALSE, LONG,  op1(I),     0 },
/*69*/  { "imul",  TRUE,  LONG,  op3(I,E,R), 0 },
/*6a*/	{ "push",  FALSE, LONG,  op1(Ibs),   0 },
/*6b*/  { "imul",  TRUE,  LONG,  op3(Ibs,E,R),0 },
/*6c*/	{ "ins",   FALSE, BYTE,  op2(DX, DI), 0 },
/*6d*/	{ "ins",   FALSE, LONG,  op2(DX, DI), 0 },
/*6e*/	{ "outs",  FALSE, BYTE,  op2(SI, DX), 0 },
/*6f*/	{ "outs",  FALSE, LONG,  op2(SI, DX), 0 },

/*70*/	{ "jo",    FALSE, NONE,  op1(Db),     0 },
/*71*/	{ "jno",   FALSE, NONE,  op1(Db),     0 },
/*72*/	{ "jb",    FALSE, NONE,  op1(Db),     0 },
/*73*/	{ "jnb",   FALSE, NONE,  op1(Db),     0 },
/*74*/	{ "jz",    FALSE, NONE,  op1(Db),     0 },
/*75*/	{ "jnz",   FALSE, NONE,  op1(Db),     0 },
/*76*/	{ "jbe",   FALSE, NONE,  op1(Db),     0 },
/*77*/	{ "jnbe",  FALSE, NONE,  op1(Db),     0 },

/*78*/	{ "js",    FALSE, NONE,  op1(Db),     0 },
/*79*/	{ "jns",   FALSE, NONE,  op1(Db),     0 },
/*7a*/	{ "jp",    FALSE, NONE,  op1(Db),     0 },
/*7b*/	{ "jnp",   FALSE, NONE,  op1(Db),     0 },
/*7c*/	{ "jl",    FALSE, NONE,  op1(Db),     0 },
/*7d*/	{ "jnl",   FALSE, NONE,  op1(Db),     0 },
/*7e*/	{ "jle",   FALSE, NONE,  op1(Db),     0 },
/*7f*/	{ "jnle",  FALSE, NONE,  op1(Db),     0 },

/*80*/  { "",	   TRUE,  BYTE,  op2(I, E),   db_Grp1 },
/*81*/  { "",	   TRUE,  LONG,  op2(I, E),   db_Grp1 },
/*82*/  { "",	   TRUE,  BYTE,  op2(I, E),   db_Grp1 },
/*83*/  { "",	   TRUE,  LONG,  op2(Ibs,E),  db_Grp1 },
/*84*/	{ "test",  TRUE,  BYTE,  op2(R, E),   0 },
/*85*/	{ "test",  TRUE,  LONG,  op2(R, E),   0 },
/*86*/	{ "xchg",  TRUE,  BYTE,  op2(R, E),   0 },
/*87*/	{ "xchg",  TRUE,  LONG,  op2(R, E),   0 },

/*88*/	{ "mov",   TRUE,  BYTE,  op2(R, E),   0 },
/*89*/	{ "mov",   TRUE,  LONG,  op2(R, E),   0 },
/*8a*/	{ "mov",   TRUE,  BYTE,  op2(E, R),   0 },
/*8b*/	{ "mov",   TRUE,  LONG,  op2(E, R),   0 },
/*8c*/  { "mov",   TRUE,  NONE,  op2(S, Ew),  0 },
/*8d*/	{ "lea",   TRUE,  LONG,  op2(E, R),   0 },
/*8e*/	{ "mov",   TRUE,  NONE,  op2(Ew, S),  0 },
/*8f*/	{ "pop",   TRUE,  LONG,  op1(E),      0 },

/*90*/	{ "nop",   FALSE, NONE,  0,	      0 },
/*91*/	{ "xchg",  FALSE, LONG,  op2(A, Ri),  0 },
/*92*/	{ "xchg",  FALSE, LONG,  op2(A, Ri),  0 },
/*93*/	{ "xchg",  FALSE, LONG,  op2(A, Ri),  0 },
/*94*/	{ "xchg",  FALSE, LONG,  op2(A, Ri),  0 },
/*95*/	{ "xchg",  FALSE, LONG,  op2(A, Ri),  0 },
/*96*/	{ "xchg",  FALSE, LONG,  op2(A, Ri),  0 },
/*97*/	{ "xchg",  FALSE, LONG,  op2(A, Ri),  0 },

/*98*/	{ "cwde",  FALSE, SDEP,  0,	      "cbw" },
/*99*/	{ "cdq",   FALSE, SDEP,  0,	      "cwd" },
/*9a*/	{ "lcall", FALSE, NONE,  op1(OS),     0 },
/*9b*/	{ "wait",  FALSE, NONE,  0,	      0 },
/*9c*/	{ "pushf", FALSE, LONG,  0,	      0 },
/*9d*/	{ "popf",  FALSE, LONG,  0,	      0 },
/*9e*/	{ "sahf",  FALSE, NONE,  0,	      0 },
/*9f*/	{ "lahf",  FALSE, NONE,  0,	      0 },

/*a0*/	{ "mov",   FALSE, BYTE,  op2(O, A),   0 },
/*a1*/	{ "mov",   FALSE, LONG,  op2(O, A),   0 },
/*a2*/	{ "mov",   FALSE, BYTE,  op2(A, O),   0 },
/*a3*/	{ "mov",   FALSE, LONG,  op2(A, O),   0 },
/*a4*/	{ "movs",  FALSE, BYTE,  op2(SI,DI),  0 },
/*a5*/	{ "movs",  FALSE, LONG,  op2(SI,DI),  0 },
/*a6*/	{ "cmps",  FALSE, BYTE,  op2(SI,DI),  0 },
/*a7*/	{ "cmps",  FALSE, LONG,  op2(SI,DI),  0 },

/*a8*/	{ "test",  FALSE, BYTE,  op2(I, A),   0 },
/*a9*/	{ "test",  FALSE, LONG,  op2(I, A),   0 },
/*aa*/	{ "stos",  FALSE, BYTE,  op1(DI),     0 },
/*ab*/	{ "stos",  FALSE, LONG,  op1(DI),     0 },
/*ac*/	{ "lods",  FALSE, BYTE,  op1(SI),     0 },
/*ad*/	{ "lods",  FALSE, LONG,  op1(SI),     0 },
/*ae*/	{ "scas",  FALSE, BYTE,  op1(SI),     0 },
/*af*/	{ "scas",  FALSE, LONG,  op1(SI),     0 },

/*b0*/	{ "mov",   FALSE, BYTE,  op2(I, Ri),  0 },
/*b1*/	{ "mov",   FALSE, BYTE,  op2(I, Ri),  0 },
/*b2*/	{ "mov",   FALSE, BYTE,  op2(I, Ri),  0 },
/*b3*/	{ "mov",   FALSE, BYTE,  op2(I, Ri),  0 },
/*b4*/	{ "mov",   FALSE, BYTE,  op2(I, Ri),  0 },
/*b5*/	{ "mov",   FALSE, BYTE,  op2(I, Ri),  0 },
/*b6*/	{ "mov",   FALSE, BYTE,  op2(I, Ri),  0 },
/*b7*/	{ "mov",   FALSE, BYTE,  op2(I, Ri),  0 },

/*b8*/	{ "mov",   FALSE, LONG,  op2(Ilq, Ri),  0 },
/*b9*/	{ "mov",   FALSE, LONG,  op2(Ilq, Ri),  0 },
/*ba*/	{ "mov",   FALSE, LONG,  op2(Ilq, Ri),  0 },
/*bb*/	{ "mov",   FALSE, LONG,  op2(Ilq, Ri),  0 },
/*bc*/	{ "mov",   FALSE, LONG,  op2(Ilq, Ri),  0 },
/*bd*/	{ "mov",   FALSE, LONG,  op2(Ilq, Ri),  0 },
/*be*/	{ "mov",   FALSE, LONG,  op2(Ilq, Ri),  0 },
/*bf*/	{ "mov",   FALSE, LONG,  op2(Ilq, Ri),  0 },

/*c0*/	{ "",	   TRUE,  BYTE,  op2(Ib, E),  db_Grp2 },
/*c1*/	{ "",	   TRUE,  LONG,  op2(Ib, E),  db_Grp2 },
/*c2*/	{ "ret",   FALSE, NONE,  op1(Iw),     0 },
/*c3*/	{ "ret",   FALSE, NONE,  0,	      0 },
/*c4*/	{ "les",   TRUE,  LONG,  op2(E, R),   0 },
/*c5*/	{ "lds",   TRUE,  LONG,  op2(E, R),   0 },
/*c6*/	{ "mov",   TRUE,  BYTE,  op2(I, E),   0 },
/*c7*/	{ "mov",   TRUE,  LONG,  op2(I, E),   0 },

/*c8*/	{ "enter", FALSE, NONE,  op2(Iw, Ib), 0 },
/*c9*/	{ "leave", FALSE, NONE,  0,           0 },
/*ca*/	{ "lret",  FALSE, NONE,  op1(Iw),     0 },
/*cb*/	{ "lret",  FALSE, NONE,  0,	      0 },
/*cc*/	{ "int",   FALSE, NONE,  op1(o3),     0 },
/*cd*/	{ "int",   FALSE, NONE,  op1(Ib),     0 },
/*ce*/	{ "into",  FALSE, NONE,  0,	      0 },
/*cf*/	{ "iret",  FALSE, NONE,  0,	      0 },

/*d0*/	{ "",	   TRUE,  BYTE,  op2(o1, E),  db_Grp2 },
/*d1*/	{ "",	   TRUE,  LONG,  op2(o1, E),  db_Grp2 },
/*d2*/	{ "",	   TRUE,  BYTE,  op2(CL, E),  db_Grp2 },
/*d3*/	{ "",	   TRUE,  LONG,  op2(CL, E),  db_Grp2 },
/*d4*/	{ "aam",   FALSE, NONE,  op1(Iba),    0 },
/*d5*/	{ "aad",   FALSE, NONE,  op1(Iba),    0 },
/*d6*/	{ ".byte\t0xd6", FALSE, NONE, 0,      0 },
/*d7*/	{ "xlat",  FALSE, BYTE,  op1(BX),     0 },

/*d8*/  { "",      TRUE,  NONE,  0,	      db_Esc8 },
/*d9*/  { "",      TRUE,  NONE,  0,	      db_Esc9 },
/*da*/  { "",      TRUE,  NONE,  0,	      db_Esca },
/*db*/  { "",      TRUE,  NONE,  0,	      db_Escb },
/*dc*/  { "",      TRUE,  NONE,  0,	      db_Escc },
/*dd*/  { "",      TRUE,  NONE,  0,	      db_Escd },
/*de*/  { "",      TRUE,  NONE,  0,	      db_Esce },
/*df*/  { "",      TRUE,  NONE,  0,	      db_Escf },

/*e0*/	{ "loopne",FALSE, NONE,  op1(Db),     0 },
/*e1*/	{ "loope", FALSE, NONE,  op1(Db),     0 },
/*e2*/	{ "loop",  FALSE, NONE,  op1(Db),     0 },
/*e3*/	{ "jrcxz", FALSE, ADEP,  op1(Db),     "jecxz" },
/*e4*/	{ "in",    FALSE, BYTE,  op2(Ib, A),  0 },
/*e5*/	{ "in",    FALSE, LONG,  op2(Ib, A) , 0 },
/*e6*/	{ "out",   FALSE, BYTE,  op2(A, Ib),  0 },
/*e7*/	{ "out",   FALSE, LONG,  op2(A, Ib) , 0 },

/*e8*/	{ "call",  FALSE, NONE,  op1(Dl),     0 },
/*e9*/	{ "jmp",   FALSE, NONE,  op1(Dl),     0 },
/*ea*/	{ "ljmp",  FALSE, NONE,  op1(OS),     0 },
/*eb*/	{ "jmp",   FALSE, NONE,  op1(Db),     0 },
/*ec*/	{ "in",    FALSE, BYTE,  op2(DX, A),  0 },
/*ed*/	{ "in",    FALSE, LONG,  op2(DX, A) , 0 },
/*ee*/	{ "out",   FALSE, BYTE,  op2(A, DX),  0 },
/*ef*/	{ "out",   FALSE, LONG,  op2(A, DX) , 0 },

/*f0*/	{ "",      FALSE, NONE,  0,	     0 },
/*f1*/	{ ".byte\t0xf1", FALSE, NONE, 0,     0 },
/*f2*/	{ "",      FALSE, NONE,  0,	     0 },
/*f3*/	{ "",      FALSE, NONE,  0,	     0 },
/*f4*/	{ "hlt",   FALSE, NONE,  0,	     0 },
/*f5*/	{ "cmc",   FALSE, NONE,  0,	     0 },
/*f6*/	{ "",      TRUE,  BYTE,  0,	     db_Grp3 },
/*f7*/	{ "",	   TRUE,  LONG,  0,	     db_Grp3 },

/*f8*/	{ "clc",   FALSE, NONE,  0,	     0 },
/*f9*/	{ "stc",   FALSE, NONE,  0,	     0 },
/*fa*/	{ "cli",   FALSE, NONE,  0,	     0 },
/*fb*/	{ "sti",   FALSE, NONE,  0,	     0 },
/*fc*/	{ "cld",   FALSE, NONE,  0,	     0 },
/*fd*/	{ "std",   FALSE, NONE,  0,	     0 },
/*fe*/	{ "",	   TRUE,  NONE,  0,	     db_Grp4 },
/*ff*/	{ "",	   TRUE,  NONE,  0,	     db_Grp5 },
};

static const struct inst db_bad_inst =
	{ "???",   FALSE, NONE,  0,	      0 }
;

#define	f_mod(rex, byte)	((byte)>>6)
#define	f_reg(rex, byte)	((((byte)>>3)&0x7) | (rex & REX_R ? 0x8 : 0x0))
#define	f_rm(rex, byte)		(((byte)&0x7) | (rex & REX_B ? 0x8 : 0x0))

#define	sib_ss(rex, byte)	((byte)>>6)
#define	sib_index(rex, byte)	((((byte)>>3)&0x7) | (rex & REX_X ? 0x8 : 0x0))
#define	sib_base(rex, byte)	(((byte)&0x7) | (rex & REX_B ? 0x8 : 0x0))

struct i_addr {
	int		is_reg;	/* if reg, reg number is in 'disp' */
	int		disp;
	const char *	base;
	const char *	index;
	int		ss;
};

static const char * const db_reg[2][4][16] = {

	{{"%al",  "%cl",  "%dl",  "%bl",  "%ah",  "%ch",  "%dh",  "%bh",
	  "%r8b", "%r9b", "%r10b", "%r11b", "%r12b", "%r13b", "%r14b", "%r15b" },
	{ "%ax",  "%cx",  "%dx",  "%bx",  "%sp",  "%bp",  "%si",  "%di",
	  "%r8w", "%r9w", "%r10w", "%r11w", "%r12w", "%r13w", "%r14w", "%r15w" },
	{ "%eax", "%ecx", "%edx", "%ebx", "%esp", "%ebp", "%esi", "%edi",
	  "%r8d", "%r9d", "%r10d", "%r11d", "%r12d", "%r13d", "%r14d", "%r15d" },
	{ "%rax", "%rcx", "%rdx", "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
	  "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15" }},

	{{"%al",  "%cl",  "%dl",  "%bl",  "%spl",  "%bpl",  "%sil",  "%dil",
	  "%r8b", "%r9b", "%r10b", "%r11b", "%r12b", "%r13b", "%r14b", "%r15b" },
	{ "%ax",  "%cx",  "%dx",  "%bx",  "%sp",  "%bp",  "%si",  "%di",
	  "%r8w", "%r9w", "%r10w", "%r11w", "%r12w", "%r13w", "%r14w", "%r15w" },
	{ "%eax", "%ecx", "%edx", "%ebx", "%esp", "%ebp", "%esi", "%edi",
	  "%r8d", "%r9d", "%r10d", "%r11d", "%r12d", "%r13d", "%r14d", "%r15d" },
	{ "%rax", "%rcx", "%rdx", "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
	  "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15" }}
};

static const char * const db_seg_reg[8] = {
	"%es", "%cs", "%ss", "%ds", "%fs", "%gs", "", ""
};

/*
 * lengths for size attributes
 */
static const int db_lengths[] = {
	1,	/* BYTE */
	2,	/* WORD */
	4,	/* LONG */
	8,	/* QUAD */
	4,	/* SNGL */
	8,	/* DBLR */
	10,	/* EXTR */
};

#define	get_value_inc(result, loc, size, is_signed) \
	result = db_get_value((loc), (size), (is_signed)); \
	(loc) += (size);

static db_addr_t
		db_disasm_esc(db_addr_t loc, int inst, int rex, int short_addr,
		    int size, const char *seg);
static void	db_print_address(const char *seg, int size, int rex,
		    struct i_addr *addrp);
static db_addr_t
		db_read_address(db_addr_t loc, int short_addr, int rex, int regmodrm,
		    struct i_addr *addrp);

/*
 * Read address at location and return updated location.
 */
static db_addr_t
db_read_address(loc, short_addr, rex, regmodrm, addrp)
	db_addr_t	loc;
	int		short_addr;
	int		rex;
	int		regmodrm;
	struct i_addr *	addrp;		/* out */
{
	int		mod, rm, sib, index, disp, size, have_sib;

	mod = f_mod(rex, regmodrm);
	rm  = f_rm(rex, regmodrm);

	if (mod == 3) {
	    addrp->is_reg = TRUE;
	    addrp->disp = rm;
	    return (loc);
	}
	addrp->is_reg = FALSE;
	addrp->index = NULL;

	if (short_addr)
	    size = LONG;
	else
	    size = QUAD;

	if ((rm & 0x7) == 4) {
	    get_value_inc(sib, loc, 1, FALSE);
	    rm = sib_base(rex, sib);
	    index = sib_index(rex, sib);
	    if (index != 4)
		addrp->index = db_reg[1][size][index];
	    addrp->ss = sib_ss(rex, sib);
	    have_sib = 1;
	} else
	    have_sib = 0;

	switch (mod) {
	    case 0:
		if (rm == 5) {
		    get_value_inc(addrp->disp, loc, 4, FALSE);
		    if (have_sib)
			addrp->base = NULL;
		    else if (short_addr)
			addrp->base = "%eip";
		    else
			addrp->base = "%rip";
		} else {
		    addrp->disp = 0;
		    addrp->base = db_reg[1][size][rm];
		}
		break;

	    case 1:
		get_value_inc(disp, loc, 1, TRUE);
		addrp->disp = disp;
		addrp->base = db_reg[1][size][rm];
		break;

	    case 2:
		get_value_inc(disp, loc, 4, FALSE);
		addrp->disp = disp;
		addrp->base = db_reg[1][size][rm];
		break;
	}
	return (loc);
}

static void
db_print_address(seg, size, rex, addrp)
	const char *	seg;
	int		size;
	int		rex;
	struct i_addr *	addrp;
{
	if (addrp->is_reg) {
	    db_printf("%s", db_reg[rex != 0 ? 1 : 0][(size == LONG && (rex & REX_W)) ? QUAD : size][addrp->disp]);
	    return;
	}

	if (seg) {
	    db_printf("%s:", seg);
	}

	if (addrp->disp != 0 || (addrp->base == NULL && addrp->index == NULL))
		db_printsym((db_addr_t)addrp->disp, DB_STGY_ANY);
	if (addrp->base != NULL || addrp->index != NULL) {
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
static db_addr_t
db_disasm_esc(loc, inst, rex, short_addr, size, seg)
	db_addr_t	loc;
	int		inst;
	int		rex;
	int		short_addr;
	int		size;
	const char *	seg;
{
	int		regmodrm;
	const struct finst *	fp;
	int		mod;
	struct i_addr	address;
	const char *	name;

	get_value_inc(regmodrm, loc, 1, FALSE);
	fp = &db_Esc_inst[inst - 0xd8][f_reg(rex, regmodrm)];
	mod = f_mod(rex, regmodrm);
	if (mod != 3) {
	    if (*fp->f_name == '\0') {
		db_printf("<bad instruction>");
		return (loc);
	    }
	    /*
	     * Normal address modes.
	     */
	    loc = db_read_address(loc, short_addr, rex, regmodrm, &address);
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
	    db_print_address(seg, BYTE, rex, &address);
	}
	else {
	    /*
	     * 'reg-reg' - special formats
	     */
	    switch (fp->f_rrmode) {
		case op2(ST,STI):
		    name = (fp->f_rrname) ? fp->f_rrname : fp->f_name;
		    db_printf("%s\t%%st,%%st(%d)",name,f_rm(rex, regmodrm));
		    break;
		case op2(STI,ST):
		    name = (fp->f_rrname) ? fp->f_rrname : fp->f_name;
		    db_printf("%s\t%%st(%d),%%st",name, f_rm(rex, regmodrm));
		    break;
		case op1(STI):
		    name = (fp->f_rrname) ? fp->f_rrname : fp->f_name;
		    db_printf("%s\t%%st(%d)",name, f_rm(rex, regmodrm));
		    break;
		case op1(X):
		    name = ((const char * const *)fp->f_rrname)[f_rm(rex, regmodrm)];
		    if (*name == '\0')
			goto bad;
		    db_printf("%s", name);
		    break;
		case op1(XA):
		    name = ((const char * const *)fp->f_rrname)[f_rm(rex, regmodrm)];
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
db_addr_t
db_disasm(db_addr_t loc, bool altfmt)
{
	int	inst;
	int	size;
	int	short_addr;
	const char *	seg;
	const struct inst *	ip;
	const char *	i_name;
	int	i_size;
	int	i_mode;
	int	rex = 0;
	int	regmodrm = 0;
	boolean_t	first;
	int	displ;
	int	prefix;
	int	rep;
	int	imm;
	int	imm2;
	long	imm64;
	int	len;
	struct i_addr	address;

	get_value_inc(inst, loc, 1, FALSE);
	short_addr = FALSE;
	size = LONG;
	seg = NULL;

	/*
	 * Get prefixes
	 */
	rep = FALSE;
	prefix = TRUE;
	do {
	    switch (inst) {
		case 0x66:		/* data16 */
		    size = WORD;
		    break;
		case 0x67:
		    short_addr = TRUE;
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
		    rep = TRUE;
		    break;
		default:
		    prefix = FALSE;
		    break;
	    }
	    if (inst >= 0x40 && inst < 0x50) {
		rex = inst;
		prefix = TRUE;
	    }
	    if (prefix) {
		get_value_inc(inst, loc, 1, FALSE);
	    }
	} while (prefix);

	if (inst >= 0xd8 && inst <= 0xdf) {
	    loc = db_disasm_esc(loc, inst, rex, short_addr, size, seg);
	    db_printf("\n");
	    return (loc);
	}

	ip = &db_inst_table[inst];
	while (ip->i_size == ESC) {
	    get_value_inc(inst, loc, 1, FALSE);
	    ip = ((const struct inst * const *)ip->i_extra)[inst>>4];
	    if (ip == NULL) {
		ip = &db_bad_inst;
	    }
	    else {
		ip = &ip[inst&0xf];
	    }
	}

	if (ip->i_has_modrm) {
	    get_value_inc(regmodrm, loc, 1, FALSE);
	    loc = db_read_address(loc, short_addr, rex, regmodrm, &address);
	}

	i_name = ip->i_name;
	i_size = ip->i_size;
	i_mode = ip->i_mode;

	if (ip->i_extra == db_Grp9 && f_mod(rex, regmodrm) == 3) {
	    ip = &db_Grp9b[f_reg(rex, regmodrm)];
	    i_name = ip->i_name;
	    i_size = ip->i_size;
	    i_mode = ip->i_mode;
	}
	else if (ip->i_extra == db_Grp1 || ip->i_extra == db_Grp2 ||
	    ip->i_extra == db_Grp6 || ip->i_extra == db_Grp7 ||
	    ip->i_extra == db_Grp8 || ip->i_extra == db_Grp9 ||
	    ip->i_extra == db_Grp15) {
	    i_name = ((const char * const *)ip->i_extra)[f_reg(rex, regmodrm)];
	}
	else if (ip->i_extra == db_Grp3) {
	    ip = ip->i_extra;
	    ip = &ip[f_reg(rex, regmodrm)];
	    i_name = ip->i_name;
	    i_mode = ip->i_mode;
	}
	else if (ip->i_extra == db_Grp4 || ip->i_extra == db_Grp5) {
	    ip = ip->i_extra;
	    ip = &ip[f_reg(rex, regmodrm)];
	    i_name = ip->i_name;
	    i_mode = ip->i_mode;
	    i_size = ip->i_size;
	}

	/* Special cases that don't fit well in the tables. */
	if (ip->i_extra == db_Grp7 && f_mod(rex, regmodrm) == 3) {
		switch (regmodrm) {
		case 0xc1:
			i_name = "vmcall";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xc2:
			i_name = "vmlaunch";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xc3:
			i_name = "vmresume";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xc4:
			i_name = "vmxoff";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xc8:
			i_name = "monitor";
			i_size = NONE;
			i_mode = 0;			
			break;
		case 0xc9:
			i_name = "mwait";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xca:
			i_name = "clac";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xcb:
			i_name = "stac";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xd0:
			i_name = "xgetbv";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xd1:
			i_name = "xsetbv";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xd8:
			i_name = "vmrun";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xd9:
			i_name = "vmmcall";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xda:
			i_name = "vmload";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xdb:
			i_name = "vmsave";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xdc:
			i_name = "stgi";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xdd:
			i_name = "clgi";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xde:
			i_name = "skinit";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xdf:
			i_name = "invlpga";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xf8:
			i_name = "swapgs";
			i_size = NONE;
			i_mode = 0;
			break;
		case 0xf9:
			i_name = "rdtscp";
			i_size = NONE;
			i_mode = 0;
			break;
		}
	}
	if (ip->i_extra == db_Grp15 && f_mod(rex, regmodrm) == 3) {
		i_name = db_Grp15b[f_reg(rex, regmodrm)];
		i_size = NONE;
		i_mode = 0;
	}

	/* Handle instructions identified by mandatory prefixes. */
	if (rep == TRUE) {
	    if (inst == 0x90) {
		i_name = "pause";
		i_size = NONE;
		i_mode = 0;
		rep = FALSE;
	    } else if (ip->i_extra == db_Grp9 && f_mod(rex, regmodrm) != 3 &&
		f_reg(rex, regmodrm) == 0x6) {
		i_name = "vmxon";
		rep = FALSE;
	    }
	}
	if (size == WORD) {
	    if (ip->i_extra == db_Grp9 && f_mod(rex, regmodrm) != 3 &&
		f_reg(rex, regmodrm) == 0x6) {
		i_name = "vmclear";
	    }
	}
	if (rex & REX_W) {
	    if (strcmp(i_name, "cwde") == 0)
		i_name = "cdqe";
	    else if (strcmp(i_name, "cmpxchg8b") == 0)
		i_name = "cmpxchg16b";
	}

	if (rep == TRUE)
	    db_printf("repe ");	/* XXX repe VS rep */

	if (i_size == SDEP) {
	    if (size == LONG)
		db_printf("%s", i_name);
	    else
		db_printf("%s", (const char *)ip->i_extra);
	} else if (i_size == ADEP) {
	    if (short_addr == FALSE)
		db_printf("%s", i_name);
	    else
		db_printf("%s", (const char *)ip->i_extra);
	}
	else {
	    db_printf("%s", i_name);
	    if ((inst >= 0x50 && inst <= 0x5f) || inst == 0x68 || inst == 0x6a) {
		i_size = NONE;
		db_printf("q");
	    }
	    if (i_size != NONE) {
		if (i_size == BYTE) {
		    db_printf("b");
		    size = BYTE;
		}
		else if (i_size == WORD) {
		    db_printf("w");
		    size = WORD;
		}
		else if (size == WORD)
		    db_printf("w");
		else {
		    if (rex & REX_W)
			db_printf("q");
		    else
			db_printf("l");
		}
	    }
	}
	db_printf("\t");
	for (first = TRUE;
	     i_mode != 0;
	     i_mode >>= 8, first = FALSE)
	{
	    if (!first)
		db_printf(",");

	    switch (i_mode & 0xFF) {

		case E:
		    db_print_address(seg, size, rex, &address);
		    break;

		case Eind:
		    db_printf("*");
		    db_print_address(seg, size, rex, &address);
		    break;

		case El:
		    db_print_address(seg, (rex & REX_W) ? QUAD : LONG, rex, &address);
		    break;

		case EL:
		    db_print_address(seg, LONG, 0, &address);
		    break;

		case Ew:
		    db_print_address(seg, WORD, rex, &address);
		    break;

		case Eb:
		    db_print_address(seg, BYTE, rex, &address);
		    break;

		case R:
		    db_printf("%s", db_reg[rex != 0 ? 1 : 0][(size == LONG && (rex & REX_W)) ? QUAD : size][f_reg(rex, regmodrm)]);
		    break;

		case Rw:
		    db_printf("%s", db_reg[rex != 0 ? 1 : 0][WORD][f_reg(rex, regmodrm)]);
		    break;

		case Rq:
		    db_printf("%s", db_reg[rex != 0 ? 1 : 0][QUAD][f_reg(rex, regmodrm)]);
		    break;

		case Ri:
		    db_printf("%s", db_reg[0][QUAD][f_rm(rex, inst)]);
		    break;

		case Ril:
		    db_printf("%s", db_reg[rex != 0 ? 1 : 0][(rex & REX_R) ? QUAD : LONG][f_rm(rex, inst)]);
		    break;

	        case Rv:
		    db_printf("%s", db_reg[rex != 0 ? 1 : 0][(size == LONG && (rex & REX_W)) ? QUAD : size][f_rm(rex, regmodrm)]);
		    break;

		case S:
		    db_printf("%s", db_seg_reg[f_reg(rex, regmodrm)]);
		    break;

		case Si:
		    db_printf("%s", db_seg_reg[f_reg(rex, inst)]);
		    break;

		case A:
		    db_printf("%s", db_reg[rex != 0 ? 1 : 0][size][0]);	/* acc */
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
		    db_printf("(%s)", short_addr ? "%si" : "%rsi");
		    break;

		case DI:
		    db_printf("%%es:(%s)", short_addr ? "%di" : "%rdi");
		    break;

		case CR:
		    db_printf("%%cr%d", f_reg(rex, regmodrm));
		    break;

		case DR:
		    db_printf("%%dr%d", f_reg(rex, regmodrm));
		    break;

		case TR:
		    db_printf("%%tr%d", f_reg(rex, regmodrm));
		    break;

		case I:
		    len = db_lengths[size];
		    get_value_inc(imm, loc, len, FALSE);
		    db_printf("$%#r", imm);
		    break;

		case Is:
		    len = db_lengths[(size == LONG && (rex & REX_W)) ? QUAD : size];
		    get_value_inc(imm, loc, len, FALSE);
		    db_printf("$%+#r", imm);
		    break;

		case Ib:
		    get_value_inc(imm, loc, 1, FALSE);
		    db_printf("$%#r", imm);
		    break;

		case Iba:
		    get_value_inc(imm, loc, 1, FALSE);
		    if (imm != 0x0a)
			db_printf("$%#r", imm);
		    break;

		case Ibs:
		    get_value_inc(imm, loc, 1, TRUE);
		    if (size == WORD)
			imm &= 0xFFFF;
		    db_printf("$%+#r", imm);
		    break;

		case Iw:
		    get_value_inc(imm, loc, 2, FALSE);
		    db_printf("$%#r", imm);
		    break;

		case Ilq:
		    len = db_lengths[rex & REX_W ? QUAD : LONG];
		    get_value_inc(imm64, loc, len, FALSE);
		    db_printf("$%#lr", imm64);
		    break;

		case O:
		    len = (short_addr ? 2 : 4);
		    get_value_inc(displ, loc, len, FALSE);
		    if (seg)
			db_printf("%s:%+#r",seg, displ);
		    else
			db_printsym((db_addr_t)displ, DB_STGY_ANY);
		    break;

		case Db:
		    get_value_inc(displ, loc, 1, TRUE);
		    displ += loc;
		    if (size == WORD)
			displ &= 0xFFFF;
		    db_printsym((db_addr_t)displ, DB_STGY_XTRN);
		    break;

		case Dl:
		    len = db_lengths[(size == LONG && (rex & REX_W)) ? QUAD : size];
		    get_value_inc(displ, loc, len, FALSE);
		    displ += loc;
		    if (size == WORD)
			displ &= 0xFFFF;
		    db_printsym((db_addr_t)displ, DB_STGY_XTRN);
		    break;

		case o1:
		    db_printf("$1");
		    break;

		case o3:
		    db_printf("$3");
		    break;

		case OS:
		    len = db_lengths[size];
		    get_value_inc(imm, loc, len, FALSE);	/* offset */
		    get_value_inc(imm2, loc, 2, FALSE);	/* segment */
		    db_printf("$%#r,%#r", imm2, imm);
		    break;
	    }
	}
	db_printf("\n");
	return (loc);
}
