/* $OpenBSD: db_disasm.c,v 1.28 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: db_disasm.c,v 1.8 2000/05/25 19:57:30 jhawk Exp $ */

/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989,1988,1987 Carnegie Mellon University
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
 */

/*
 *	File: db_disasm.c
 * 	Author: Alessandro Forin, Carnegie Mellon University
 *	Date:	11/91
 *
 *	Disassembler for Alpha
 *
 *	Modified for NetBSD/alpha by:
 *
 *	Christopher G. Demetriou, Carnegie Mellon University
 *
 *	Jason R. Thorpe, Numerical Aerospace Simulation Facility,
 *	NASA Ames Research Center
 *
 *	This code was derived exclusively from information available in
 *	"Alpha Architecture Reference Manual", Richard L. Sites ed.
 *	Digital Press, Burlington, MA 01803
 *	ISBN 1-55558-098-X, Order no. EY-L520E-DP
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <machine/db_machdep.h>
#include <alpha/alpha/db_instruction.h>

#include <machine/pal.h>

#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>
#include <ddb/db_interface.h>

/*
 * This would belong in a header file, except noone else needs it
 *
 * XXX THESE SHOULD BE CONVERTED TO ra, rb, rc FORMAT.
 */
typedef union {
	/*
	 *	All instructions are 32 bits wide, PAL included
	 */
	unsigned int	bits;

	/*
	 *	Internal processor register access instrs
	 *	specify the IPR index, doubly specify the
	 *	(same) GP register as src/dest, and qualifiers
	 *	for the IPR set involved (abox/ibox/tmp)
	 */
	struct {
		unsigned	index : 5,
				regset : 3, /* a,i,p */
				xxx : 8,
				rs : 5,
				rd : 5,
				opcode : 6;
	} mXpr_format;

	/*
	 *	Load/store instructions have a 12 bit displacement,
	 *	and two register specifiers just as normal ld/st.
	 *	Four bits have special meanings:
	 *		phy: bypass the MMU (physical access)
	 *		alt: use mode in ALT register for checks,
	 *		     or if PHY is also on locked/linked access
	 *		rwc: read-with-write-check (probew)
	 *		qw:  quadword access
	 */
	struct {
		signed int	displacement : 12;
		unsigned	qw : 1,
				qualif : 3,
				rs : 5,
				rd : 5,
				opcode : 6;
	} mem_format;

	/*
	 *	Return from exception or interrupt has
	 *	a branch-like encoding, but only one
	 *	instantiation is actually usable.
	 */
	struct {
		unsigned	xxx : 14,
				zero : 1,	/* branch prediction! */
				one : 1,
				rb : 5,		/* r31 or stall */
				ra : 5,		/* r31 or stall */
				opcode : 6;
	} rei_format;

} pal_instruction;


/*
 * Major opcodes
 */
static char *op_name[64] = {
/* 0 */	"call_pal", "op1", "op2", "op3", "op4",	"op5",	"op6",	"op7",
/* 8 */	"lda",	"ldah",	"ldbu",	"ldq_u","ldwu",	"stw",	"stb",	"stq_u",
/*16 */	"arit",	"logical","bit","mul",	"op20",	"vaxf",	"ieeef","anyf",
/*24 */	"spec",	"hw_mfpr","jump","hw_ld","intmisc","hw_mtpr","hw_rei","hw_st",
/*32 */	"ldf",	"ldg",	"lds",	"ldt",	"stf",	"stg",	"sts",	"stt",
/*40 */	"ldl",	"ldq",	"ldl_l","ldq_l","stl",	"stq",	"stl_c","stq_c",
/*48 */	"br",	"fbeq",	"fblt",	"fble",	"bsr",	"fbne",	"fbge",	"fbgt",
/*56 */	"blbc",	"beq",	"blt",	"ble",	"blbs",	"bne",	"bge",	"bgt"
};

/*
 * The function field is too big (7 or 11 bits), so the sub-tables
 * are addressed in a somewhat complicated manner to save
 * space.  After all, alu operations is what RISCs are good at.
 */

struct tbl {
	const char	*name;
	int		code;
};

static const struct tbl pal_op_tbl[] = {
	/* Common PAL function codes. */
	{ "halt",		PAL_halt },
	{ "cflush",		PAL_cflush },
	{ "draina",		PAL_draina },
	{ "cserve",		PAL_cserve, },
	{ "swppal",		PAL_swppal },
	{ "ipir",		PAL_ipir },
	{ "bpt",		PAL_bpt },
	{ "bugchk",		PAL_bugchk },
	{ "imb",		PAL_imb },
	{ "rdunique",		PAL_rdunique },
	{ "wrunique",		PAL_wrunique },
	{ "gentrap",		PAL_gentrap },

	/* OSF/1 PAL function codes. */
	{ "osf1_rdmces",	PAL_OSF1_rdmces },
	{ "osf1_wrmces",	PAL_OSF1_wrmces },
	{ "osf1_wrfen",		PAL_OSF1_wrfen },
	{ "osf1_wrvptptr",	PAL_OSF1_wrvptptr },
	{ "osf1_swpctx",	PAL_OSF1_swpctx },
	{ "osf1_wrval",		PAL_OSF1_wrval },
	{ "osf1_rdval",		PAL_OSF1_rdval },
	{ "osf1_tbi",		PAL_OSF1_tbi },
	{ "osf1_wrent",		PAL_OSF1_wrent },
	{ "osf1_swpipl",	PAL_OSF1_swpipl },
	{ "osf1_rdps",		PAL_OSF1_rdps },
	{ "osf1_wrkgp",		PAL_OSF1_wrkgp },
	{ "osf1_wrusp",		PAL_OSF1_wrusp },
	{ "osf1_wrperfmon",	PAL_OSF1_wrperfmon },
	{ "osf1_rdusp",		PAL_OSF1_rdusp },
	{ "osf1_whami",		PAL_OSF1_whami },
	{ "osf1_retsys",	PAL_OSF1_retsys },
	{ "osf1_rti",		PAL_OSF1_rti },
	{ "osf1_callsys",	PAL_OSF1_callsys },

	{ NULL,			-1 },
};

static const char *pal_opname(int);

static const char *
pal_opname(int op)
{
	static char unk[11];
	int i;

	for (i = 0; pal_op_tbl[i].name != NULL; i++) {
		if (pal_op_tbl[i].code == op)
			return (pal_op_tbl[i].name);
	}

	snprintf(unk, sizeof unk, "0x%x", op);
	return (unk);
}

/* HW (PAL) instruction qualifiers, stright tables */
static const char *mXpr_name[8] = {
	"", "/i", "/a", "/ai", "/p", "/pi", "/pa", "/pai"
};
static const char *hwlds_name[8] = {
	"", "/r", "/a", "/ar", "/p", "/p?r", "_l-c", "_l-c/?r"
};

/*
 * For this one we take the low nibble (valid values 0/2/9/b/d)
 * and shift it down one to get the row index.  Within a row
 * we can just take the high nibble deprived of the high bit
 * (valid values 0/1/2/3/4/6).  We could have used a flat 64
 * entry array, but in this way we use just 48 pointers.
 * BUGFIX: the 'cmpbge 0x0f' opcode fits in here too
 */
static const char *arit_c0[8] = {
	"addl", 0, "addq", 0, "addl/v", 0, "addq/v",
};
static const char *arit_c2[8] = {
	"s4addl", "s8addl", "s4addq", "s8addq",
};
static const char *arit_c9[8] = {
	"subl", 0, "subq", 0, "subl/v", 0, "subq/v",
};
static const char *arit_cB[8] = {
	"s4subl", "s8subl", "s4subq", "s8subq",
};
static const char *arit_cD[8] = {
	0, "cmpult", "cmpeq", "cmpule", "cmplt", 0, "cmple",
};
static const char *arit_cF[1] = {
	"cmpbge"
};
static const char **arit_opname[8] = {
	arit_c0, arit_c2, 0, 0, arit_c9, arit_cB, arit_cD, arit_cF
};

static __inline const char *arit_name(int);
static __inline const char *
arit_name(int op)
{
	static char unk[32];
	const char *name = NULL;

	if (arit_opname[((op)&0xe)>>1])
		name = arit_opname[((op)&0xe)>>1][((op)&0x70)>>4];

	if (name != NULL)
		return (name);

	snprintf(unk, sizeof unk, "?arit 0x%x?", op);
	return (unk);
}

/*
 * Something similar for this one, except there are only
 * 16 entries so the row indexing is done by enumeration
 * of the low nibble (valid values 0/4/6/8).  Then we can
 * just shift the high nibble to index inside the row
 * (valid values are 0/2/4 or 1/2/4/6)
 *
 * There are two functions that don't play by these simple rules,
 * so we special-case them.
 */
static const char *logical_c0[4] = {
	"and", "or", "xor", 0
};
static const char *logical_c4[4] = {
	"cmovlbs", "cmoveq", "cmovlt", "cmovle"
};
static const char *logical_c6[4] = {
	"cmovlbc", "cmovne", "cmovge", "cmovgt"
};
static const char *logical_c8[4] = {
	"andnot", "ornot", "xornot", 0
};

static __inline const char *logical_name(int);
static __inline const char *
logical_name(int op)
{
	static char unk[32];
	const char *name = NULL;

	if (op == op_amask)
		return ("amask");
	else if (op == op_implver)
		return ("implver");

	switch (op & 0xf) {
	case 0: name = logical_c0[((op)>>5)&3]; break;
	case 4: name = logical_c4[((op)>>5)&3]; break;
	case 6: name = logical_c6[((op)>>5)&3]; break;
	case 8: name = logical_c8[((op)>>5)&3]; break;
	}

	if (name != NULL)
		return (name);

	snprintf(unk, sizeof unk, "?logical 0x%x?", op);
	return (unk);
}

/*
 * This is the messy one. First, we single out the dense
 * case of a 3 in the high nibble (valid values 0/1/2/4/6/9/b/c).
 * Then the case of a 2 in the low nibble (valid values 0/1/2/5/6/7).
 * For the remaining codes (6/7/a/b) we do as above: high
 * nibble has valid values 0/1/2 or 5/6/7.  The low nibble
 * can be used as row index picking bits 0 and 2, for the
 * high one just the lower two bits.
 */
static const char *bitop_c3[8] = {
	"zapnot", "mskql", "srl", "extql", "sll", "insql", "sra", 0
};
static const char *bitop_c2[8] = {
	"mskbl", "mskwl", "mskll", 0/*mskql*/, 0, "mskwh", "msklh", "mskqh"
};
static const char *bitop_c67ab[4][4] = {
/* a */	{ 0, "extwh", "extlh", "extqh"},
/* b */	{ "insbl", "inswl", "insll", 0 },
/* 6 */	{ "extbl", "extwl", "extll", 0 },
/* 7 */	{ 0, "inswh", "inslh", "insqh" },
};

static __inline const char *bitop_name(int);
static __inline const char *
bitop_name(int op)
{
	static char unk[32];
	const char *name = NULL;

	if ((op & 0x70) == 0x30)
		name = (op == op_zap) ? "zap" : bitop_c3[((op)&0xe)>>1];
	else if ((op & 0xf) == 0x02)
		name = bitop_c2[(op)>>4];
	else
		name =
		    bitop_c67ab[(((op)&1)|(((op)&0x4)>>1))][(((op)&0x30)>>4)];

	if (name != NULL)
		return (name);

	snprintf(unk, sizeof unk, "?bit 0x%x?", op);
	return (unk);
}

/*
 * Only 5 entries in this one
 */
static const char *mul_opname[4] = {
	"mull", "mulq", "mull/v", "mulq/v"
};

static __inline const char *mul_name(int);
static __inline const char *
mul_name(int op)
{
	static char unk[32];
	const char *name = NULL;

	name = (op == op_umulh) ? "umulh" : mul_opname[((op)>>5)&3];

	if (name != NULL)
		return (name);

	snprintf(unk, sizeof unk, "?mul 0x%x?", op);
	return (unk);
}

/*
 * These are few, the high nibble is usually enough to dispatch.
 * We single out the `f' case to halve the table size, as
 * well as the cases in which the high nibble isn't enough.
 */
static const char *special_opname[8] = {
	"trapb", 0, "mb", 0, "fetch", "fetch_m", "rpcc", "rc"
};

static __inline const char *special_name(int);
static __inline const char *
special_name(int op)
{
	static char unk[32];
	const char *name;

	switch (op) {
	case op_excb:		name = "excb";		break;
	case op_wmb:		name = "wmb";		break;
	case op_ecb:		name = "ecb";		break;
	case op_rs:		name = "rs";		break;
	case op_wh64:		name = "wh64";		break;
	default:
		name = special_opname[(op)>>13];
	}

	if (name != NULL)
		return (name);

	snprintf(unk, sizeof unk, "?special 0x%x?", op);
	return (unk);
}

/*
 * This is trivial
 */
static const char *jump_opname[4] = {
	"jmp", "jsr", "ret", "jcr"
};
#define jump_name(ix)	jump_opname[ix]

/*
 * For all but 4 of these, we can dispatch on the lower nibble of
 * the "function".
 */
static const char *intmisc_opname_3x[16] = {
	"ctpop", "perr", "ctlz", "cttz", "unpkbw", "unpkbl", "pkwb",
	"pklb", "minsb8", "minsw4", "minub8", "minuw4", "maxub8",
	"maxuw4", "maxsb8", "maxsw4",
};

static __inline const char *intmisc_name(int);
static __inline const char *
intmisc_name(int op)
{
	static char unk[32];

	if ((op & 0xf0) == 0x30)
		return (intmisc_opname_3x[op & 0x0f]);

	switch (op) {
	case op_sextb: return ("sextb");
	case op_sextw: return ("sextw");
	case op_ftoit: return ("ftoit");
	case op_ftois: return ("ftois");
	}

	snprintf(unk, sizeof unk, "?intmisc 0x%x?", op);
	return (unk);
}

static const char *float_name(const struct tbl[], int, const char *type);

static const char *
float_name(const struct tbl tbl[], int op, const char *type)
{
	static char unk[32];
	int i;

	for (i = 0; tbl[i].name != NULL; i++) {
		if (tbl[i].code == op)
			return (tbl[i].name);
	}

	snprintf(unk, sizeof unk, "?%s 0x%x?", type, op);
	return (unk);
}

#define vaxf_name(op)	float_name(vaxf_tbl, op, "vaxfl")
#define ieeef_name(op)	float_name(ieeef_tbl, op, "ieeefl")
#define anyf_name(op)	float_name(anyf_tbl, op, "anyfl")

static const struct tbl anyf_tbl[] = {
	{ "cvtlq",	0x010},
	{ "cpys",	0x020},
	{ "cpysn",	0x021},
	{ "cpyse",	0x022},
	{ "mt_fpcr",	0x024},
	{ "mf_fpcr",	0x025},
	{ "fcmoveq",	0x02a},
	{ "fcmovne",	0x02b},
	{ "fcmovlt",	0x02c},
	{ "fcmovge",	0x02d},
	{ "fcmovle",	0x02e},
	{ "fcmovgt",	0x02f},
	{ "cvtql",	0x030},
	{ "cvtql/v",	0x130},
	{ "cvtql/sv",	0x530},
	{ 0, 0},
};

static const struct tbl ieeef_tbl[] = {
	{ "adds/c",	0x000},
	{ "subs/c",	0x001},
	{ "muls/c",	0x002},
	{ "divs/c",	0x003},
	{ "addt/c",	0x020},
	{ "subt/c",	0x021},
	{ "mult/c",	0x022},
	{ "divt/c",	0x023},
	{ "cvtts/c",	0x02c},
	{ "cvttq/c",	0x02f},
	{ "cvtqs/c",	0x03c},
	{ "cvtqt/c",	0x03e},
	{ "adds/m",	0x040},
	{ "subs/m",	0x041},
	{ "muls/m",	0x042},
	{ "divs/m",	0x043},
	{ "addt/m",	0x060},
	{ "subt/m",	0x061},
	{ "mult/m",	0x062},
	{ "divt/m",	0x063},
	{ "cvtts/m",	0x06c},
	{ "cvtqs/m",	0x07c},
	{ "cvtqt/m",	0x07e},
	{ "adds",	0x080},
	{ "subs",	0x081},
	{ "muls",	0x082},
	{ "divs",	0x083},
	{ "addt",	0x0a0},
	{ "subt",	0x0a1},
	{ "mult",	0x0a2},
	{ "divt",	0x0a3},
	{ "cmptun",	0x0a4},
	{ "cmpteq",	0x0a5},
	{ "cmptlt",	0x0a6},
	{ "cmptle",	0x0a7},
	{ "cvtts",	0x0ac},
	{ "cvttq",	0x0af},
	{ "cvtqs",	0x0bc},
	{ "cvtqt",	0x0be},
	{ "adds/d",	0x0c0},
	{ "subs/d",	0x0c1},
	{ "muls/d",	0x0c2},
	{ "divs/d",	0x0c3},
	{ "addt/d",	0x0e0},
	{ "subt/d",	0x0e1},
	{ "mult/d",	0x0e2},
	{ "divt/d",	0x0e3},
	{ "cvtts/d",	0x0ec},
	{ "cvtqs/d",	0x0fc},
	{ "cvtqt/d",	0x0fe},
	{ "adds/uc",	0x100},
	{ "subs/uc",	0x101},
	{ "muls/uc",	0x102},
	{ "divs/uc",	0x103},
	{ "addt/uc",	0x120},
	{ "subt/uc",	0x121},
	{ "mult/uc",	0x122},
	{ "divt/uc",	0x123},
	{ "cvtts/uc",	0x12c},
	{ "cvttq/vc",	0x12f},
	{ "adds/um",	0x140},
	{ "subs/um",	0x141},
	{ "muls/um",	0x142},
	{ "divs/um",	0x143},
	{ "addt/um",	0x160},
	{ "subt/um",	0x161},
	{ "mult/um",	0x162},
	{ "divt/um",	0x163},
	{ "cvtts/um",	0x16c},
	{ "adds/u",	0x180},
	{ "subs/u",	0x181},
	{ "muls/u",	0x182},
	{ "divs/u",	0x183},
	{ "addt/u",	0x1a0},
	{ "subt/u",	0x1a1},
	{ "mult/u",	0x1a2},
	{ "divt/u",	0x1a3},
	{ "cvtts/u",	0x1ac},
	{ "cvttq/v",	0x1af},
	{ "adds/ud",	0x1c0},
	{ "subs/ud",	0x1c1},
	{ "muls/ud",	0x1c2},
	{ "divs/ud",	0x1c3},
	{ "addt/ud",	0x1e0},
	{ "subt/ud",	0x1e1},
	{ "mult/ud",	0x1e2},
	{ "divt/ud",	0x1e3},
	{ "cvtts/ud",	0x1ec},
	{ "adds/suc",	0x500},
	{ "subs/suc",	0x501},
	{ "muls/suc",	0x502},
	{ "divs/suc",	0x503},
	{ "addt/suc",	0x520},
	{ "subt/suc",	0x521},
	{ "mult/suc",	0x522},
	{ "divt/suc",	0x523},
	{ "cvtts/suc",	0x52c},
	{ "cvttq/svc",	0x52f},
	{ "adds/sum",	0x540},
	{ "subs/sum",	0x541},
	{ "muls/sum",	0x542},
	{ "divs/sum",	0x543},
	{ "addt/sum",	0x560},
	{ "subt/sum",	0x561},
	{ "mult/sum",	0x562},
	{ "divt/sum",	0x563},
	{ "cvtts/sum",	0x56c},
	{ "adds/su",	0x580},
	{ "subs/su",	0x581},
	{ "muls/su",	0x582},
	{ "divs/su",	0x583},
	{ "addt/su",	0x5a0},
	{ "subt/su",	0x5a1},
	{ "mult/su",	0x5a2},
	{ "divt/su",	0x5a3},
	{ "cmptun/su",	0x5a4},
	{ "cmpteq/su",	0x5a5},
	{ "cmptlt/su",	0x5a6},
	{ "cmptle/su",	0x5a7},
	{ "cvtts/su",	0x5ac},
	{ "cvttq/sv",	0x5af},
	{ "adds/sud",	0x5c0},
	{ "subs/sud",	0x5c1},
	{ "muls/sud",	0x5c2},
	{ "divs/sud",	0x5c3},
	{ "addt/sud",	0x5e0},
	{ "subt/sud",	0x5e1},
	{ "mult/sud",	0x5e2},
	{ "divt/sud",	0x5e3},
	{ "cvtts/sud",	0x5ec},
	{ "adds/suic",	0x700},
	{ "subs/suic",	0x701},
	{ "muls/suic",	0x702},
	{ "divs/suic",	0x703},
	{ "addt/suic",	0x720},
	{ "subt/suic",	0x721},
	{ "mult/suic",	0x722},
	{ "divt/suic",	0x723},
	{ "cvtts/suic",	0x72c},
	{ "cvttq/svic",	0x72f},
	{ "cvtqs/suic",	0x73c},
	{ "cvtqt/suic",	0x73e},
	{ "adds/suim",	0x740},
	{ "subs/suim",	0x741},
	{ "muls/suim",	0x742},
	{ "divs/suim",	0x743},
	{ "addt/suim",	0x760},
	{ "subt/suim",	0x761},
	{ "mult/suim",	0x762},
	{ "divt/suim",	0x763},
	{ "cvtts/suim",	0x76c},
	{ "cvtqs/suim",	0x77c},
	{ "cvtqt/suim",	0x77e},
	{ "adds/sui",	0x780},
	{ "subs/sui",	0x781},
	{ "muls/sui",	0x782},
	{ "divs/sui",	0x783},
	{ "addt/sui",	0x7a0},
	{ "subt/sui",	0x7a1},
	{ "mult/sui",	0x7a2},
	{ "divt/sui",	0x7a3},
	{ "cvtts/sui",	0x7ac},
	{ "cvttq/svi",	0x7af},
	{ "cvtqs/sui",	0x7bc},
	{ "cvtqt/sui",	0x7be},
	{ "adds/suid",	0x7c0},
	{ "subs/suid",	0x7c1},
	{ "muls/suid",	0x7c2},
	{ "divs/suid",	0x7c3},
	{ "addt/suid",	0x7e0},
	{ "subt/suid",	0x7e1},
	{ "mult/suid",	0x7e2},
	{ "divt/suid",	0x7e3},
	{ "cvtts/suid",	0x7ec},
	{ "cvtqs/suid",	0x7fc},
	{ "cvtqt/suid",	0x7fe},
	{ 0, 0}
};

static const struct tbl vaxf_tbl[] = {
	{ "addf/c",	0x000},
	{ "subf/c",	0x001},
	{ "mulf/c",	0x002},
	{ "divf/c",	0x003},
	{ "cvtdg/c",	0x01e},
	{ "addg/c",	0x020},
	{ "subg/c",	0x021},
	{ "mulg/c",	0x022},
	{ "divg/c",	0x023},
	{ "cvtgf/c",	0x02c},
	{ "cvtgd/c",	0x02d},
	{ "cvtgq/c",	0x02f},
	{ "cvtqf/c",	0x03c},
	{ "cvtqg/c",	0x03e},
	{ "addf",	0x080},
	{ "subf",	0x081},
	{ "mulf",	0x082},
	{ "divf",	0x083},
	{ "cvtdg",	0x09e},
	{ "addg",	0x0a0},
	{ "subg",	0x0a1},
	{ "mulg",	0x0a2},
	{ "divg",	0x0a3},
	{ "cmpgeq",	0x0a5},
	{ "cmpglt",	0x0a6},
	{ "cmpgle",	0x0a7},
	{ "cvtgf",	0x0ac},
	{ "cvtgd",	0x0ad},
	{ "cvtgq",	0x0af},
	{ "cvtqf",	0x0bc},
	{ "cvtqg",	0x0be},
	{ "addf/uc",	0x100},
	{ "subf/uc",	0x101},
	{ "mulf/uc",	0x102},
	{ "divf/uc",	0x103},
	{ "cvtdg/uc",	0x11e},
	{ "addg/uc",	0x120},
	{ "subg/uc",	0x121},
	{ "mulg/uc",	0x122},
	{ "divg/uc",	0x123},
	{ "cvtgf/uc",	0x12c},
	{ "cvtgd/uc",	0x12d},
	{ "cvtgq/vc",	0x12f},
	{ "addf/u",	0x180},
	{ "subf/u",	0x181},
	{ "mulf/u",	0x182},
	{ "divf/u",	0x183},
	{ "cvtdg/u",	0x19e},
	{ "addg/u",	0x1a0},
	{ "subg/u",	0x1a1},
	{ "mulg/u",	0x1a2},
	{ "divg/u",	0x1a3},
	{ "cvtgf/u",	0x1ac},
	{ "cvtgd/u",	0x1ad},
	{ "cvtgq/v",	0x1af},
	{ "addf/sc",	0x400},
	{ "subf/sc",	0x401},
	{ "mulf/sc",	0x402},
	{ "divf/sc",	0x403},
	{ "cvtdg/sc",	0x41e},
	{ "addg/sc",	0x420},
	{ "subg/sc",	0x421},
	{ "mulg/sc",	0x422},
	{ "divg/sc",	0x423},
	{ "cvtgf/sc",	0x42c},
	{ "cvtgd/sc",	0x42d},
	{ "cvtgq/sc",	0x42f},
	{ "cvtqf/sc",	0x43c},
	{ "cvtqg/sc",	0x43e},
	{ "addf/s",	0x480},
	{ "subf/s",	0x481},
	{ "mulf/s",	0x482},
	{ "divf/s",	0x483},
	{ "cvtdg/s",	0x49e},
	{ "addg/s",	0x4a0},
	{ "subg/s",	0x4a1},
	{ "mulg/s",	0x4a2},
	{ "divg/s",	0x4a3},
	{ "cmpgeq/s",	0x4a5},
	{ "cmpglt/s",	0x4a6},
	{ "cmpgle/s",	0x4a7},
	{ "cvtgf/s",	0x4ac},
	{ "cvtgd/s",	0x4ad},
	{ "cvtgq/s",	0x4af},
	{ "cvtqf/s",	0x4bc},
	{ "cvtqg/s",	0x4be},
	{ "addf/suc",	0x500},
	{ "subf/suc",	0x501},
	{ "mulf/suc",	0x502},
	{ "divf/suc",	0x503},
	{ "cvtdg/suc",	0x51e},
	{ "addg/suc",	0x520},
	{ "subg/suc",	0x521},
	{ "mulg/suc",	0x522},
	{ "divg/suc",	0x523},
	{ "cvtgf/suc",	0x52c},
	{ "cvtgd/suc",	0x52d},
	{ "cvtgq/svc",	0x52f},
	{ "addf/su",	0x580},
	{ "subf/su",	0x581},
	{ "mulf/su",	0x582},
	{ "divf/su",	0x583},
	{ "cvtdg/su",	0x59e},
	{ "addg/su",	0x5a0},
	{ "subg/su",	0x5a1},
	{ "mulg/su",	0x5a2},
	{ "divg/su",	0x5a3},
	{ "cvtgf/su",	0x5ac},
	{ "cvtgd/su",	0x5ad},
	{ "cvtgq/sv",	0x5af},
	{ 0, 0}
};

/*
 * General purpose registers
 */
static const char *name_of_register[32] = {
	"v0",	"t0",	"t1",	"t2",	"t3",	"t4",	"t5",	"t6",
	"t7",	"s0",	"s1",	"s2",	"s3",	"s4",	"s5",	"s6",
	"a0",	"a1",	"a2",	"a3",	"a4",	"a5",	"t8",	"t9",
	"t10",	"t11",	"ra",	"pv",	"at",	"gp",	"sp",	"zero"
};

static int regcount;		/* how many regs used in this inst */
static int regnum[3];		/* which regs used in this inst */

static const char *register_name(int);

static const char *
register_name(int ireg)
{
	int	i;

	for (i = 0; i < regcount; i++)
		if (regnum[i] == ireg)
			break;
	if (i >= regcount)
		regnum[regcount++] = ireg;
	return (name_of_register[ireg]);
}

/*
 * Disassemble instruction at 'loc'.  'altfmt' specifies an
 * (optional) alternate format.  Return address of start of
 * next instruction.
 */
int	alpha_print_instruction(vaddr_t, alpha_instruction, int);

vaddr_t
db_disasm(vaddr_t loc, int altfmt)
{
	alpha_instruction inst;

	inst.bits = db_get_value(loc, 4, 0);

	loc += alpha_print_instruction(loc, inst, altfmt);
	return (loc);
}

int
alpha_print_instruction(vaddr_t iadr, alpha_instruction i, int showregs)
{
	const char	*opcode;
	int		ireg;
	long		signed_immediate;
	int		fstore;
	pal_instruction	p;
	char		tmpfmt[28];

	regcount = 0;
	fstore = 0;
	opcode = op_name[i.mem_format.opcode];

	/*
	 *	Dispatch directly on the opcode, save code
	 *	duplication sometimes via "harmless gotos".
	 */
	switch (i.mem_format.opcode) {
	case op_pal:
		/* "call_pal" is a long string; just use a space. */
		db_printf("%s %s", opcode, pal_opname(i.pal_format.function));
		break;
	case op_lda:
	case op_ldah:
	case op_ldbu:
	case op_ldq_u:
	case op_ldwu:
	case op_stw:
	case op_stb:
	case op_stq_u:
		/*
		 * These loadstores are here to make compiling the
		 * switch a bit easier.  Could embellish the output
		 * someday, too.
		 */
		goto loadstore;
		break;
	case op_arit:
		/*
		 * For this and the following three groups we
		 * just need different opcode strings
		 */
		opcode = arit_name(i.operate_lit_format.function);
		goto operate;
		break;
	case op_logical:
		opcode = logical_name(i.operate_lit_format.function);
		goto operate;
		break;
	case op_bit:
		opcode = bitop_name(i.operate_lit_format.function);
		goto operate;
		break;
	case op_mul:
		opcode = mul_name(i.operate_lit_format.function);
operate:
		/*
		 * Nice and uniform, just check for literals
		 */
		db_printf("%s\t%s,", opcode,
		    register_name(i.operate_lit_format.ra));
		if (i.operate_lit_format.one)
			db_printf("#0x%x", i.operate_lit_format.literal);
		else
			db_printf("%s", register_name(i.operate_reg_format.rb));
		db_printf(",%s", register_name(i.operate_lit_format.rc));
		break;
	case op_vax_float:
		/*
		 * The three floating point groups are even simpler
		 */
		opcode = vaxf_name(i.float_format.function);
		goto foperate;
		break;
	case op_ieee_float:
		opcode = ieeef_name(i.float_format.function);
		goto foperate;
		break;
	case op_any_float:
		opcode = anyf_name(i.float_format.function);
foperate:
		db_printf("%s\tf%d,f%d,f%d", opcode,
			i.float_format.fa,
			i.float_format.fb,
			i.float_format.fc);
		break;
	case op_special:
		/*
		 * Miscellaneous.
		 */
		{
			register unsigned int code;

			code = (i.mem_format.displacement)&0xffff;
			opcode = special_name(code);

			switch (code) {
			case op_ecb:
				db_printf("%s\t(%s)", opcode,
					register_name(i.mem_format.rb));
				break;
			case op_fetch:
			case op_fetch_m:
				db_printf("%s\t0(%s)", opcode,
					register_name(i.mem_format.rb));
				break;
			case op_rpcc:
			case op_rc:
			case op_rs:
				db_printf("%s\t%s", opcode,
					register_name(i.mem_format.ra));
				break;
			default:
				db_printf("%s", opcode);
			break;
			}
		}
		break;
	case op_j:
		/*
		 * Jump instructions really are of two sorts,
		 * depending on the use of the hint info.
		 */
		opcode = jump_name(i.jump_format.action);
		switch (i.jump_format.action) {
		case op_jmp:
		case op_jsr:
			db_printf("%s\t%s,(%s),", opcode,
				register_name(i.jump_format.ra),
				register_name(i.jump_format.rb));
			signed_immediate = i.jump_format.hint;
			goto branch_displacement;
			break;
		case op_ret:
		case op_jcr:
			db_printf("%s\t%s,(%s)", opcode,
				register_name(i.jump_format.ra),
				register_name(i.jump_format.rb));
			break;
		}
		break;
	case op_intmisc:
		/*
		 * These are just in "operate" format.
		 */
		opcode = intmisc_name(i.operate_lit_format.function);
		goto operate;
		break;
			/* HW instructions, possibly chip-specific XXXX */
	case op_pal19:	/* "hw_mfpr" */
	case op_pal1d:	/* "hw_mtpr" */
		p.bits = i.bits;
		db_printf("\t%s%s\t%s, %d", opcode,
			mXpr_name[p.mXpr_format.regset],
			register_name(p.mXpr_format.rd),
			p.mXpr_format.index);
		break;
	case op_pal1b:	/* "hw_ld" */
	case op_pal1f:	/* "hw_st" */
		p.bits = i.bits;
		db_printf("\t%s%c%s\t%s,", opcode,
			(p.mem_format.qw) ? 'q' : 'l',
			hwlds_name[p.mem_format.qualif],
			register_name(p.mem_format.rd));
		signed_immediate = (long)p.mem_format.displacement;
		goto loadstore_address;

	case op_pal1e:	/* "hw_rei" */
		db_printf("\t%s", opcode);
		break;

	case op_ldf:
	case op_ldg:
	case op_lds:
	case op_ldt:
	case op_stf:
	case op_stg:
	case op_sts:
	case op_stt:
		fstore = 1;
		/* FALLTHROUGH */
	case op_ldl:
	case op_ldq:
	case op_ldl_l:
	case op_ldq_l:
	case op_stl:
	case op_stq:
	case op_stl_c:
	case op_stq_c:
		/*
		 * Memory operations, including floats
		 */
loadstore:
		if (fstore)
		    db_printf("%s\tf%d,", opcode, i.mem_format.ra);
		else
		    db_printf("%s\t%s,", opcode,
		        register_name(i.mem_format.ra));
		signed_immediate = (long)i.mem_format.displacement;
loadstore_address:
		db_printf("%s(%s)", db_format(tmpfmt, sizeof tmpfmt,
		    signed_immediate, DB_FORMAT_Z, 0, 0),
		    register_name(i.mem_format.rb));
		/*
		 * For convenience, do the address computation
		 */
		if (showregs) {
			if (i.mem_format.opcode == op_ldah)
				signed_immediate <<= 16;
			db_printf(" <0x%lx>", signed_immediate +
			    db_register_value(&ddb_regs, i.mem_format.rb));
		}
		break;
	case op_br:
	case op_fbeq:
	case op_fblt:
	case op_fble:
	case op_bsr:
	case op_fbne:
	case op_fbge:
	case op_fbgt:
	case op_blbc:
	case op_beq:
	case op_blt:
	case op_ble:
	case op_blbs:
	case op_bne:
	case op_bge:
	case op_bgt:
		/*
		 * We want to know where we are branching to
		 */
		signed_immediate = (long)i.branch_format.displacement;
		db_printf("%s\t%s,", opcode,
			  register_name(i.branch_format.ra));
branch_displacement:
		db_printsym(iadr + sizeof(alpha_instruction) +
		    (signed_immediate << 2), DB_STGY_PROC, db_printf);
		break;
	default:
		/*
		 * Shouldn't happen
		 */
		db_printf("? 0x%x ?", i.bits);
	}

	/*
	 *	Print out the registers used in this instruction
	 */
	if (showregs && regcount > 0) {
		db_printf("\t<");
		for (ireg = 0; ireg < regcount; ireg++) {
			if (ireg != 0)
				db_printf(",");
			db_printf("%s=0x%lx",
			    name_of_register[regnum[ireg]],
			    db_register_value(&ddb_regs, regnum[ireg]));
		}
		db_printf(">");
	}
	db_printf("\n");
	return (sizeof(alpha_instruction));
}
