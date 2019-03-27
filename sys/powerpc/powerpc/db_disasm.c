/*	$NetBSD: db_disasm.c,v 1.28 2013/07/04 23:00:23 joerg Exp $	*/
/*	$OpenBSD: db_disasm.c,v 1.2 1996/12/28 06:21:48 rahnds Exp $	*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>
#include <ddb/db_output.h>

enum function_mask {
	Op_A    =	0x00000001,
	Op_B    =	0x00000002,
	Op_BI   =	0x00000004,
	Op_BO   =	0x00000008,
	Op_BC   =	Op_BI | Op_BO,
	Op_CRM  =	0x00000010,
	Op_D    =	0x00000020,
	Op_ST   =	0x00000020,  /* Op_S for store-operations, same as D */
	Op_S    =	0x00000040,  /* S-field is swapped with A-field */
	Op_FM   =	Op_D | Op_S, /* kludge (reduce Op_s) */
	Op_dA  =	0x00000080,
	Op_LK   =	0x00000100,
	Op_Rc   =	0x00000200,
	Op_AA	=	Op_LK | Op_Rc, /* kludge (reduce Op_s) */
	Op_LKM	=	Op_AA,
	Op_RcM	=	Op_AA,
	Op_OE   =	0x00000400,
	Op_SR   =	0x00000800,
	Op_TO   =	0x00001000,
	Op_sign =	0x00002000,
	Op_const =	0x00004000,
	Op_SIMM =	Op_const | Op_sign,
	Op_UIMM =	Op_const,
	Op_crbA =	0x00008000,
	Op_crbB =	0x00010000,
	Op_WS	=	Op_crbB,	/* kludge, same field as crbB */
	Op_rSH	=	Op_crbB,	/* kludge, same field as crbB */
	Op_crbD =	0x00020000,
	Op_crfD =	0x00040000,
	Op_crfS =	0x00080000,
	Op_ds   =	0x00100000,
	Op_me   =	0x00200000,
	Op_spr  =	0x00400000,
	Op_dcr  =	Op_spr,		/* out of bits - cheat with Op_spr */
	Op_tbr  =	0x00800000,

	Op_BP	=	0x01000000,
	Op_BD	=	0x02000000,
	Op_LI	=	0x04000000,
	Op_C	=	0x08000000,

	Op_NB	=	0x10000000,

	Op_sh_mb_sh =	0x20000000,
	Op_sh   =	0x40000000,
	Op_SH	=	Op_sh | Op_sh_mb_sh,
	Op_mb	=	0x80000000,
	Op_MB	=	Op_mb | Op_sh_mb_sh,
	Op_ME	=	Op_MB,

};

struct opcode {
	const char *name;
	u_int32_t mask;
	u_int32_t code;
	enum function_mask func;
};

typedef u_int32_t instr_t;
typedef void (op_class_func) (instr_t, vm_offset_t);

u_int32_t extract_field(u_int32_t value, u_int32_t base, u_int32_t width);
void disasm_fields(const struct opcode *popcode, instr_t instr, vm_offset_t loc,
    char *disasm_str, size_t slen);
void dis_ppc(const struct opcode *opcodeset, instr_t instr, vm_offset_t loc);

op_class_func op_ill, op_base;
op_class_func op_cl_x13, op_cl_x1e, op_cl_x1f;
op_class_func op_cl_x3a, op_cl_x3b;
op_class_func op_cl_x3e, op_cl_x3f;

op_class_func *opcodes_base[] = {
/*x00*/	op_ill,		op_ill,		op_base,	op_ill,
/*x04*/	op_ill,		op_ill,		op_ill,		op_base,
/*x08*/	op_base,	op_base,	op_base,	op_base,
/*x0C*/ op_base,	op_base,	op_base/*XXX*/,	op_base/*XXX*/,
/*x10*/ op_base,	op_base,	op_base,	op_cl_x13,
/*x14*/	op_base,	op_base,	op_ill,		op_base,
/*x18*/	op_base,	op_base,	op_base,	op_base,
/*x1C*/ op_base,	op_base,	op_cl_x1e,	op_cl_x1f,
/*x20*/	op_base,	op_base,	op_base,	op_base,
/*x24*/	op_base,	op_base,	op_base,	op_base,
/*x28*/	op_base,	op_base,	op_base,	op_base,
/*x2C*/	op_base,	op_base,	op_base,	op_base,
/*x30*/	op_base,	op_base,	op_base,	op_base,
/*x34*/	op_base,	op_base,	op_base,	op_base,
/*x38*/ op_ill,		op_ill,		op_cl_x3a,	op_cl_x3b,
/*x3C*/	op_ill,		op_ill,		op_cl_x3e,	op_cl_x3f
};


/* This table could be modified to make significant the "reserved" fields
 * of the opcodes, But I didn't feel like it when typing in the table,
 * I would recommend that this table be looked over for errors, 
 * This was derived from the table in Appendix A.2 of (Mot part # MPCFPE/AD)
 * PowerPC Microprocessor Family: The Programming Environments
 */
	
const struct opcode opcodes[] = {
	{ "tdi",	0xfc000000, 0x08000000, Op_TO | Op_A | Op_SIMM },
	{ "twi",	0xfc000000, 0x0c000000, Op_TO | Op_A | Op_SIMM },
	{ "mulli",	0xfc000000, 0x1c000000, Op_D | Op_A | Op_SIMM },
	{ "subfic",	0xfc000000, 0x20000000, Op_D | Op_A | Op_SIMM },
	{ "cmplwi",	0xfc200000, 0x28000000, Op_crfD | Op_A | Op_SIMM },
	{ "cmpldi",	0xfc200000, 0x28200000, Op_crfD | Op_A | Op_SIMM },
	{ "cmpwi",	0xfc200000, 0x2c000000, Op_crfD | Op_A | Op_SIMM },
	{ "cmpdi",	0xfc200000, 0x2c200000, Op_crfD | Op_A | Op_SIMM },
	{ "addic",	0xfc000000, 0x30000000, Op_D | Op_A | Op_SIMM },
	{ "addic.",	0xfc000000, 0x34000000, Op_D | Op_A | Op_SIMM },
	{ "addi",	0xfc000000, 0x38000000, Op_D | Op_A | Op_SIMM },
	{ "addis",	0xfc000000, 0x3c000000, Op_D | Op_A | Op_SIMM },
	{ "b",		0xfc000000, 0x40000000, Op_BC | Op_BD | Op_AA | Op_LK }, /* bc */
	{ "sc",		0xffffffff, 0x44000002, 0 },
	{ "b",		0xfc000000, 0x48000000, Op_LI | Op_AA | Op_LK },

	{ "rlwimi",	0xfc000000, 0x50000000, Op_S | Op_A | Op_SH | Op_MB | Op_ME | Op_Rc },
	{ "rlwinm",	0xfc000000, 0x54000000, Op_S | Op_A | Op_SH | Op_MB | Op_ME | Op_Rc },
	{ "rlwnm",	0xfc000000, 0x5c000000, Op_S | Op_A | Op_SH | Op_MB | Op_ME | Op_Rc },

	{ "ori",	0xfc000000, 0x60000000, Op_S | Op_A | Op_UIMM },
	{ "oris",	0xfc000000, 0x64000000, Op_S | Op_A | Op_UIMM },
	{ "xori",	0xfc000000, 0x68000000, Op_S | Op_A | Op_UIMM },
	{ "xoris",	0xfc000000, 0x6c000000, Op_S | Op_A | Op_UIMM },

	{ "andi.",	0xfc000000, 0x70000000, Op_S | Op_A | Op_UIMM },
	{ "andis.",	0xfc000000, 0x74000000, Op_S | Op_A | Op_UIMM },

	{ "lwz",	0xfc000000, 0x80000000, Op_D | Op_dA },
	{ "lwzu",	0xfc000000, 0x84000000, Op_D | Op_dA },
	{ "lbz",	0xfc000000, 0x88000000, Op_D | Op_dA },
	{ "lbzu",	0xfc000000, 0x8c000000, Op_D | Op_dA },
	{ "stw",	0xfc000000, 0x90000000, Op_ST | Op_dA },
	{ "stwu",	0xfc000000, 0x94000000, Op_ST | Op_dA },
	{ "stb",	0xfc000000, 0x98000000, Op_ST | Op_dA },
	{ "stbu",	0xfc000000, 0x9c000000, Op_ST | Op_dA },

	{ "lhz",	0xfc000000, 0xa0000000, Op_D | Op_dA },
	{ "lhzu",	0xfc000000, 0xa4000000, Op_D | Op_dA },
	{ "lha",	0xfc000000, 0xa8000000, Op_D | Op_dA },
	{ "lhau",	0xfc000000, 0xac000000, Op_D | Op_dA },
	{ "sth",	0xfc000000, 0xb0000000, Op_ST | Op_dA },
	{ "sthu",	0xfc000000, 0xb4000000, Op_ST | Op_dA },
	{ "lmw",	0xfc000000, 0xb8000000, Op_D | Op_dA },
	{ "stmw",	0xfc000000, 0xbc000000, Op_ST | Op_dA },

	{ "lfs",	0xfc000000, 0xc0000000, Op_D | Op_dA },
	{ "lfsu",	0xfc000000, 0xc4000000, Op_D | Op_dA },
	{ "lfd",	0xfc000000, 0xc8000000, Op_D | Op_dA },
	{ "lfdu",	0xfc000000, 0xcc000000, Op_D | Op_dA },

	{ "stfs",	0xfc000000, 0xd0000000, Op_ST | Op_dA },
	{ "stfsu",	0xfc000000, 0xd4000000, Op_ST | Op_dA },
	{ "stfd",	0xfc000000, 0xd8000000, Op_ST | Op_dA },
	{ "stfdu",	0xfc000000, 0xdc000000, Op_ST | Op_dA },
	{ "",		0x0,		0x0, 0 }

};
/* 13 * 4 = 4c */
const struct opcode opcodes_13[] = {
/* 0x13 << 2 */
	{ "mcrf",	0xfc0007fe, 0x4c000000, Op_crfD | Op_crfS },
	{ "b",		0xfc0007fe, 0x4c000020, Op_BC | Op_LK }, /* bclr */
	{ "crnor",	0xfc0007fe, 0x4c000042, Op_crbD | Op_crbA | Op_crbB },
	{ "rfi",	0xfc0007fe, 0x4c000064, 0 },
	{ "crandc",	0xfc0007fe, 0x4c000102, Op_crbD | Op_crbA | Op_crbB },
	{ "isync",	0xfc0007fe, 0x4c00012c, 0 },
	{ "crxor",	0xfc0007fe, 0x4c000182, Op_crbD | Op_crbA | Op_crbB },
	{ "crnand",	0xfc0007fe, 0x4c0001c2, Op_crbD | Op_crbA | Op_crbB },
	{ "crand",	0xfc0007fe, 0x4c000202, Op_crbD | Op_crbA | Op_crbB },
	{ "creqv",	0xfc0007fe, 0x4c000242, Op_crbD | Op_crbA | Op_crbB },
	{ "crorc",	0xfc0007fe, 0x4c000342, Op_crbD | Op_crbA | Op_crbB },
	{ "cror",	0xfc0007fe, 0x4c000382, Op_crbD | Op_crbA | Op_crbB },
	{ "b",		0xfc0007fe, 0x4c000420, Op_BC | Op_LK }, /* bcctr */
	{ "",		0x0,		0x0, 0 }
};

/* 1e * 4 = 78 */
const struct opcode opcodes_1e[] = {
	{ "rldicl",	0xfc00001c, 0x78000000, Op_S | Op_A | Op_sh | Op_mb | Op_Rc },
	{ "rldicr",	0xfc00001c, 0x78000004, Op_S | Op_A | Op_sh | Op_me | Op_Rc },
	{ "rldic",	0xfc00001c, 0x78000008, Op_S | Op_A | Op_sh | Op_mb | Op_Rc },
	{ "rldimi",	0xfc00001c, 0x7800000c, Op_S | Op_A | Op_sh | Op_mb | Op_Rc },
	{ "rldcl",	0xfc00003e, 0x78000010, Op_S | Op_A | Op_B | Op_mb | Op_Rc },
	{ "rldcr",	0xfc00003e, 0x78000012, Op_S | Op_A | Op_B | Op_me | Op_Rc },
	{ "",		0x0,		0x0, 0 }
};

/* 1f * 4 = 7c */
const struct opcode opcodes_1f[] = {
/* 1f << 2 */
	{ "cmpw",	0xfc2007fe, 0x7c000000, Op_crfD | Op_A | Op_B },
	{ "cmpd",	0xfc2007fe, 0x7c200000, Op_crfD | Op_A | Op_B },
	{ "tw",		0xfc0007fe, 0x7c000008, Op_TO | Op_A | Op_B },
	{ "subfc",	0xfc0003fe, 0x7c000010, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "mulhdu",	0xfc0007fe, 0x7c000012, Op_D | Op_A | Op_B | Op_Rc },
	{ "addc",	0xfc0003fe, 0x7c000014, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "mulhwu",	0xfc0007fe, 0x7c000016, Op_D | Op_A | Op_B | Op_Rc },
	{ "isellt",	0xfc0007ff, 0x7c00001e, Op_D | Op_A | Op_B },
	{ "iselgt",	0xfc0007ff, 0x7c00005e, Op_D | Op_A | Op_B },
	{ "iseleq",	0xfc0007ff, 0x7c00009e, Op_D | Op_A | Op_B },

	{ "mfcr",	0xfc0007fe, 0x7c000026, Op_D },
	{ "lwarx",	0xfc0007fe, 0x7c000028, Op_D | Op_A | Op_B },
	{ "ldx",	0xfc0007fe, 0x7c00002a, Op_D | Op_A | Op_B },
	{ "lwzx",	0xfc0007fe, 0x7c00002e, Op_D | Op_A | Op_B },
	{ "slw",	0xfc0007fe, 0x7c000030, Op_D | Op_A | Op_B | Op_Rc },
	{ "cntlzw",	0xfc0007fe, 0x7c000034, Op_D | Op_A | Op_Rc },
	{ "sld",	0xfc0007fe, 0x7c000036, Op_D | Op_A | Op_B | Op_Rc },
	{ "and",	0xfc0007fe, 0x7c000038, Op_D | Op_A | Op_B | Op_Rc },
	{ "cmplw",	0xfc2007fe, 0x7c000040, Op_crfD | Op_A | Op_B },
	{ "cmpld",	0xfc2007fe, 0x7c200040, Op_crfD | Op_A | Op_B },
	{ "subf",	0xfc0003fe, 0x7c000050, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "ldux",	0xfc0007fe, 0x7c00006a, Op_D | Op_A | Op_B },
	{ "dcbst",	0xfc0007fe, 0x7c00006c, Op_A | Op_B },
	{ "lwzux",	0xfc0007fe, 0x7c00006e, Op_D | Op_A | Op_B },
	{ "cntlzd",	0xfc0007fe, 0x7c000074, Op_S | Op_A | Op_Rc },
	{ "andc",	0xfc0007fe, 0x7c000078, Op_S | Op_A | Op_B | Op_Rc },
	{ "td",		0xfc0007fe, 0x7c000088, Op_TO | Op_A | Op_B },
	{ "mulhd",	0xfc0007fe, 0x7c000092, Op_D | Op_A | Op_B | Op_Rc },
	{ "mulhw",	0xfc0007fe, 0x7c000096, Op_D | Op_A | Op_B | Op_Rc },
	{ "mfmsr",	0xfc0007fe, 0x7c0000a6, Op_D },
	{ "ldarx",	0xfc0007fe, 0x7c0000a8, Op_D | Op_A | Op_B },
	{ "dcbf",	0xfc0007fe, 0x7c0000ac, Op_A | Op_B },
	{ "lbzx",	0xfc0007fe, 0x7c0000ae, Op_D | Op_A | Op_B },
	{ "neg",	0xfc0003fe, 0x7c0000d0, Op_D | Op_A | Op_OE | Op_Rc },
	{ "lbzux",	0xfc0007fe, 0x7c0000ee, Op_D | Op_A | Op_B },
	{ "nor",	0xfc0007fe, 0x7c0000f8, Op_S | Op_A | Op_B | Op_Rc },
	{ "wrtee",	0xfc0003ff, 0x7c000106, Op_S },
	{ "subfe",	0xfc0003fe, 0x7c000110, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "adde",	0xfc0003fe, 0x7c000114, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "mtcrf",	0xfc0007fe, 0x7c000120, Op_S | Op_CRM },
	{ "mtmsr",	0xfc0007fe, 0x7c000124, Op_S },
	{ "stdx",	0xfc0007fe, 0x7c00012a, Op_ST | Op_A | Op_B },
	{ "stwcx.",	0xfc0007ff, 0x7c00012d, Op_ST | Op_A | Op_B },
	{ "stwx",	0xfc0007fe, 0x7c00012e, Op_ST | Op_A | Op_B },
	{ "wrteei",	0xfc0003fe, 0x7c000146, 0 },	/* XXX: out of flags! */
	{ "stdux",	0xfc0007fe, 0x7c00016a, Op_ST | Op_A | Op_B },
	{ "stwux",	0xfc0007fe, 0x7c00016e, Op_ST | Op_A | Op_B },
	{ "subfze",	0xfc0003fe, 0x7c000190, Op_D | Op_A | Op_OE | Op_Rc },
	{ "addze",	0xfc0003fe, 0x7c000194, Op_D | Op_A | Op_OE | Op_Rc },
	{ "mtsr",	0xfc0007fe, 0x7c0001a4, Op_S | Op_SR },
	{ "stdcx.",	0xfc0007ff, 0x7c0001ad, Op_ST | Op_A | Op_B },
	{ "stbx",	0xfc0007fe, 0x7c0001ae, Op_ST | Op_A | Op_B },
	{ "subfme",	0xfc0003fe, 0x7c0001d0, Op_D | Op_A | Op_OE | Op_Rc },
	{ "mulld",	0xfc0003fe, 0x7c0001d2, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "addme",	0xfc0003fe, 0x7c0001d4, Op_D | Op_A | Op_OE | Op_Rc },
	{ "mullw",	0xfc0003fe, 0x7c0001d6, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "mtsrin",	0xfc0007fe, 0x7c0001e4, Op_S | Op_B },
	{ "dcbtst",	0xfc0007fe, 0x7c0001ec, Op_A | Op_B },
	{ "stbux",	0xfc0007fe, 0x7c0001ee, Op_ST | Op_A | Op_B },
	{ "add",	0xfc0003fe, 0x7c000214, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "dcbt",	0xfc0007fe, 0x7c00022c, Op_A | Op_B },
	{ "lhzx",	0xfc0007ff, 0x7c00022e, Op_D | Op_A | Op_B },
	{ "eqv",	0xfc0007fe, 0x7c000238, Op_S | Op_A | Op_B | Op_Rc },
	{ "tlbie",	0xfc0007fe, 0x7c000264, Op_B },
	{ "eciwx",	0xfc0007fe, 0x7c00026c, Op_D | Op_A | Op_B },
	{ "lhzux",	0xfc0007fe, 0x7c00026e, Op_D | Op_A | Op_B },
	{ "xor",	0xfc0007fe, 0x7c000278, Op_S | Op_A | Op_B | Op_Rc },
	{ "mfdcr",	0xfc0007fe, 0x7c000286, Op_D | Op_dcr },
	{ "mfspr",	0xfc0007fe, 0x7c0002a6, Op_D | Op_spr },
	{ "lwax",	0xfc0007fe, 0x7c0002aa, Op_D | Op_A | Op_B },
	{ "lhax",	0xfc0007fe, 0x7c0002ae, Op_D | Op_A | Op_B },
	{ "tlbia",	0xfc0007fe, 0x7c0002e4, 0 },
	{ "mftb",	0xfc0007fe, 0x7c0002e6, Op_D | Op_tbr },
	{ "lwaux",	0xfc0007fe, 0x7c0002ea, Op_D | Op_A | Op_B },
	{ "lhaux",	0xfc0007fe, 0x7c0002ee, Op_D | Op_A | Op_B },
	{ "sthx",	0xfc0007fe, 0x7c00032e, Op_ST | Op_A | Op_B },
	{ "orc",	0xfc0007fe, 0x7c000338, Op_S | Op_A | Op_B | Op_Rc },
	{ "ecowx",	0xfc0007fe, 0x7c00036c, Op_ST | Op_A | Op_B | Op_Rc },
	{ "slbie",	0xfc0007fc, 0x7c000364, Op_B },
	{ "sthux",	0xfc0007fe, 0x7c00036e, Op_ST | Op_A | Op_B },
	{ "or",		0xfc0007fe, 0x7c000378, Op_S | Op_A | Op_B | Op_Rc },
	{ "mtdcr",	0xfc0007fe, 0x7c000386, Op_S | Op_dcr },
	{ "divdu",	0xfc0003fe, 0x7c000392, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "divwu",	0xfc0003fe, 0x7c000396, Op_D | Op_A | Op_B | Op_OE | Op_Rc },
	{ "mtspr",	0xfc0007fe, 0x7c0003a6, Op_S | Op_spr },
	{ "dcbi",	0xfc0007fe, 0x7c0003ac, Op_A | Op_B },
	{ "nand",	0xfc0007fe, 0x7c0003b8, Op_S | Op_A | Op_B | Op_Rc },
	{ "dcread",	0xfc0007fe, 0x7c0003cc, Op_D | Op_A | Op_B },
	{ "divd",	0xfc0003fe, 0x7c0003d2, Op_S | Op_A | Op_B | Op_OE | Op_Rc },
	{ "divw",	0xfc0003fe, 0x7c0003d6, Op_S | Op_A | Op_B | Op_OE | Op_Rc },
	{ "slbia",	0xfc0003fe, 0x7c0003e4, Op_S | Op_A | Op_B | Op_OE | Op_Rc },
	{ "mcrxr",	0xfc0007fe, 0x7c000400, Op_crfD },
	{ "lswx",	0xfc0007fe, 0x7c00042a, Op_D | Op_A | Op_B },
	{ "lwbrx",	0xfc0007fe, 0x7c00042c, Op_D | Op_A | Op_B },
	{ "lfsx",	0xfc0007fe, 0x7c00042e, Op_D | Op_A | Op_B },
	{ "srw",	0xfc0007fe, 0x7c000430, Op_S | Op_A | Op_B | Op_Rc },
	{ "srd",	0xfc0007fe, 0x7c000436, Op_S | Op_A | Op_B | Op_Rc },
	{ "tlbsync",	0xfc0007fe, 0x7c00046c, 0 },
	{ "lfsux",	0xfc0007fe, 0x7c00046e, Op_D | Op_A | Op_B },
	{ "mfsr",	0xfc0007fe, 0x7c0004a6, Op_D | Op_SR },
	{ "lswi",	0xfc0007fe, 0x7c0004aa, Op_D | Op_A | Op_NB },
	{ "sync",	0xfc6007fe, 0x7c0004ac, 0 },
	{ "lwsync",	0xfc6007fe, 0x7c2004ac, 0 },
	{ "ptesync",	0xfc6007fe, 0x7c4004ac, 0 },
	{ "lfdx",	0xfc0007fe, 0x7c0004ae, Op_D | Op_A | Op_B },
	{ "lfdux",	0xfc0007fe, 0x7c0004ee, Op_D | Op_A | Op_B },
	{ "mfsrin",	0xfc0007fe, 0x7c000526, Op_D | Op_B },
	{ "stswx",	0xfc0007fe, 0x7c00052a, Op_ST | Op_A | Op_B },
	{ "stwbrx",	0xfc0007fe, 0x7c00052c, Op_ST | Op_A | Op_B },
	{ "stfsx",	0xfc0007fe, 0x7c00052e, Op_ST | Op_A | Op_B },
	{ "stfsux",	0xfc0007fe, 0x7c00056e, Op_ST | Op_A | Op_B },
	{ "stswi",	0xfc0007fe, 0x7c0005aa, Op_ST | Op_A | Op_NB },
	{ "stfdx",	0xfc0007fe, 0x7c0005ae, Op_ST | Op_A | Op_B },
	{ "stfdux",	0xfc0007fe, 0x7c0005ee, Op_ST | Op_A | Op_B },
	{ "lhbrx",	0xfc0007fe, 0x7c00062c, Op_D | Op_A | Op_B },
	{ "sraw",	0xfc0007fe, 0x7c000630, Op_S | Op_A | Op_B },
	{ "srad",	0xfc0007fe, 0x7c000634, Op_S | Op_A | Op_B | Op_Rc },
	{ "srawi",	0xfc0007fe, 0x7c000670, Op_S | Op_A | Op_rSH | Op_Rc },
	{ "sradi",	0xfc0007fc, 0x7c000674, Op_S | Op_A | Op_sh },
	{ "eieio",	0xfc0007fe, 0x7c0006ac, 0 },
	{ "tlbsx",	0xfc0007fe, 0x7c000724, Op_S | Op_A | Op_B | Op_Rc },
	{ "sthbrx",	0xfc0007fe, 0x7c00072c, Op_ST | Op_A | Op_B },
	{ "extsh",	0xfc0007fe, 0x7c000734, Op_S | Op_A | Op_Rc },
	{ "tlbre",	0xfc0007fe, 0x7c000764, Op_D | Op_A | Op_WS },
	{ "extsb",	0xfc0007fe, 0x7c000774, Op_S | Op_A | Op_Rc },
	{ "icbi",	0xfc0007fe, 0x7c0007ac, Op_A | Op_B },
	{ "tlbwe",	0xfc0007fe, 0x7c0007a4, Op_S | Op_A | Op_WS },
	{ "stfiwx",	0xfc0007fe, 0x7c0007ae, Op_ST | Op_A | Op_B },
	{ "extsw",	0xfc0007fe, 0x7c0007b4, Op_S | Op_A | Op_Rc },
	{ "dcbz",	0xfc0007fe, 0x7c0007ec, Op_A | Op_B },
	{ "",		0x0,		0x0, 0 }
};

/* 3a * 4 = e8 */
const struct opcode opcodes_3a[] = {
	{ "ld",		0xfc000003, 0xe8000000, Op_D | Op_A | Op_ds },
	{ "ldu",	0xfc000003, 0xe8000001, Op_D | Op_A | Op_ds },
	{ "lwa",	0xfc000003, 0xe8000002, Op_D | Op_A | Op_ds },
	{ "",		0x0,		0x0, 0 }
};
/* 3b * 4 = ec */
const struct opcode opcodes_3b[] = {
	{ "fdivs",	0xfc00003e, 0xec000024, Op_D | Op_A | Op_B | Op_Rc },
	{ "fsubs",	0xfc00003e, 0xec000028, Op_D | Op_A | Op_B | Op_Rc },

	{ "fadds",	0xfc00003e, 0xec00002a, Op_D | Op_A | Op_B | Op_Rc },
	{ "fsqrts",	0xfc00003e, 0xec00002c, Op_D | Op_B | Op_Rc },
	{ "fres",	0xfc00003e, 0xec000030, Op_D | Op_B | Op_Rc },
	{ "fmuls",	0xfc00003e, 0xec000032, Op_D | Op_A | Op_C | Op_Rc },
	{ "fmsubs",	0xfc00003e, 0xec000038, Op_D | Op_A | Op_B | Op_C | Op_Rc },
	{ "fmadds",	0xfc00003e, 0xec00003a, Op_D | Op_A | Op_B | Op_C | Op_Rc },
	{ "fnmsubs",	0xfc00003e, 0xec00003c, Op_D | Op_A | Op_B | Op_C | Op_Rc },
	{ "fnmadds",	0xfc00003e, 0xec00003e, Op_D | Op_A | Op_B | Op_C | Op_Rc },
	{ "",		0x0,		0x0, 0 }
};
/* 3e * 4 = f8 */
const struct opcode opcodes_3e[] = {
	{ "std",	0xfc000003, 0xf8000000, Op_ST | Op_A | Op_ds },
	{ "stdu",	0xfc000003, 0xf8000001, Op_ST | Op_A | Op_ds },
	{ "",		0x0,		0x0, 0 }
};

/* 3f * 4 = fc */
const struct opcode opcodes_3f[] = {
	{ "fcmpu",	0xfc0007fe, 0xfc000000, Op_crfD | Op_A | Op_B },
	{ "frsp",	0xfc0007fe, 0xfc000018, Op_D | Op_B | Op_Rc },
	{ "fctiw",	0xfc0007fe, 0xfc00001c, Op_D | Op_B | Op_Rc },
	{ "fctiwz",	0xfc0007fe, 0xfc00001e, Op_D | Op_B | Op_Rc },

	{ "fdiv",	0xfc00003e, 0xfc000024, Op_D | Op_A | Op_B | Op_Rc },
	{ "fsub",	0xfc00003e, 0xfc000028, Op_D | Op_A | Op_B | Op_Rc },
	{ "fadd",	0xfc00003e, 0xfc00002a, Op_D | Op_A | Op_B | Op_Rc },
	{ "fsqrt",	0xfc00003e, 0xfc00002c, Op_D | Op_B | Op_Rc },
	{ "fsel",	0xfc00003e, 0xfc00002e, Op_D | Op_A | Op_B | Op_C | Op_Rc },
	{ "fmul",	0xfc00003e, 0xfc000032, Op_D | Op_A | Op_C | Op_Rc },
	{ "frsqrte",	0xfc00003e, 0xfc000034, Op_D | Op_B | Op_Rc },
	{ "fmsub",	0xfc00003e, 0xfc000038, Op_D | Op_A | Op_B | Op_C | Op_Rc },
	{ "fmadd",	0xfc00003e, 0xfc00003a, Op_D | Op_A | Op_B | Op_C | Op_Rc },
	{ "fnmsub",	0xfc00003e, 0xfc00003c, Op_D | Op_A | Op_B | Op_C | Op_Rc },
	{ "fnmadd",	0xfc00003e, 0xfc00003e, Op_D | Op_A | Op_B | Op_C | Op_Rc },

	{ "fcmpo",	0xfc0007fe, 0xfc000040, Op_crfD | Op_A | Op_B },
	{ "mtfsb1",	0xfc0007fe, 0xfc00004c, Op_crfD | Op_Rc },
	{ "fneg",	0xfc0007fe, 0xfc000050, Op_D | Op_B | Op_Rc },
	{ "mcrfs",	0xfc0007fe, 0xfc000080, Op_D | Op_B | Op_Rc },
	{ "mtfsb0",	0xfc0007fe, 0xfc00008c, Op_crfD | Op_Rc },
	{ "fmr",	0xfc0007fe, 0xfc000090, Op_D | Op_B | Op_Rc },
	{ "mtfsfi",	0xfc0007fe, 0xfc00010c, 0 },	/* XXX: out of flags! */

	{ "fnabs",	0xfc0007fe, 0xfc000110, Op_D | Op_B | Op_Rc },
	{ "fabs",	0xfc0007fe, 0xfc000210, Op_D | Op_B | Op_Rc },
	{ "mffs",	0xfc0007fe, 0xfc00048e, Op_D | Op_B | Op_Rc },
	{ "mtfsf",	0xfc0007fe, 0xfc00058e, Op_FM | Op_B | Op_Rc },
	{ "fctid",	0xfc0007fe, 0xfc00065c, Op_D | Op_B | Op_Rc },
	{ "fctidz",	0xfc0007fe, 0xfc00065e, Op_D | Op_B | Op_Rc },
	{ "fcfid",	0xfc0007fe, 0xfc00069c, Op_D | Op_B | Op_Rc },
	{ "",		0x0,		0x0, 0 }
};


struct specialreg {
	int reg;
	const char *name;
};

const struct specialreg sprregs[] = {
	{ 0x000, "mq" },
	{ 0x001, "xer" },
	{ 0x008, "lr" },
	{ 0x009, "ctr" },
	{ 0x012, "dsisr" },
	{ 0x013, "dar" },
	{ 0x016, "dec" },
	{ 0x019, "sdr1" },
	{ 0x01a, "srr0" },
	{ 0x01b, "srr1" },
#ifdef BOOKE_PPC4XX
	{ 0x100, "usprg0" },
#else
	{ 0x100, "vrsave" },
#endif
	{ 0x110, "sprg0" },
	{ 0x111, "sprg1" },
	{ 0x112, "sprg2" },
	{ 0x113, "sprg3" },
	{ 0x114, "sprg4" },
	{ 0x115, "sprg5" },
	{ 0x116, "sprg6" },
	{ 0x117, "sprg7" },
	{ 0x118, "asr" },
	{ 0x11a, "aer" },
	{ 0x11c, "tbl" },
	{ 0x11d, "tbu" },
	{ 0x11f, "pvr" },
	{ 0x210, "ibat0u" },
	{ 0x211, "ibat0l" },
	{ 0x212, "ibat1u" },
	{ 0x213, "ibat1l" },
	{ 0x214, "ibat2u" },
	{ 0x215, "ibat2l" },
	{ 0x216, "ibat3u" },
	{ 0x217, "ibat3l" },
	{ 0x218, "dbat0u" },
	{ 0x219, "dbat0l" },
	{ 0x21a, "dbat1u" },
	{ 0x21b, "dbat1l" },
	{ 0x21c, "dbat2u" },
	{ 0x21d, "dbat2l" },
	{ 0x21e, "dbat3u" },
	{ 0x21f, "dbat3l" },
	{ 0x230, "ibat4u" },
	{ 0x231, "ibat4l" },
	{ 0x232, "ibat5u" },
	{ 0x233, "ibat5l" },
	{ 0x234, "ibat6u" },
	{ 0x235, "ibat6l" },
	{ 0x236, "ibat7u" },
	{ 0x237, "ibat7l" },
	{ 0x238, "dbat4u" },
	{ 0x239, "dbat4l" },
	{ 0x23a, "dbat5u" },
	{ 0x23b, "dbat5l" },
	{ 0x23c, "dbat6u" },
	{ 0x23d, "dbat6l" },
	{ 0x23e, "dbat7u" },
	{ 0x23f, "dbat7l" },
	{ 0x3b0, "zpr" },
	{ 0x3b1, "pid" },
	{ 0x3b3, "ccr0" },
	{ 0x3b4, "iac3" },
	{ 0x3b5, "iac4" },
	{ 0x3b6, "dvc1" },
	{ 0x3b7, "dvc2" },
	{ 0x3b9, "sgr" },
	{ 0x3ba, "dcwr" },
	{ 0x3bb, "sler" },
	{ 0x3bc, "su0r" },
	{ 0x3bd, "dbcr1" },
	{ 0x3d3, "icdbdr" },
	{ 0x3d4, "esr" },
	{ 0x3d5, "dear" },
	{ 0x3d6, "evpr" },
	{ 0x3d8, "tsr" },
	{ 0x3da, "tcr" },
	{ 0x3db, "pit" },
	{ 0x3de, "srr2" },
	{ 0x3df, "srr3" },
#ifdef BOOKE_PPC4XX
	{ 0x3f0, "dbsr" },
	{ 0x3f2, "dbcr0" },
	{ 0x3f4, "iac1" },
	{ 0x3f5, "iac2" },
	{ 0x3f6, "dac1" },
	{ 0x3f7, "dac2" },
#else
	{ 0x3f0, "hid0" },
	{ 0x3f1, "hid1" },
	{ 0x3f2, "iabr" },
	{ 0x3f3, "hid2" },
	{ 0x3f5, "dabr" },
	{ 0x3f6, "msscr0" },
	{ 0x3f7, "msscr1" },
#endif
	{ 0x3f9, "l2cr" },
	{ 0x3fa, "dccr" },
	{ 0x3fb, "iccr" },
	{ 0x3ff, "pir" },
	{ 0, NULL }
};

const struct specialreg dcrregs[] = {
	{ 0x010, "sdram0_cfgaddr" },
	{ 0x011, "sdram0_cfgdata" },
	{ 0x012, "ebc0_cfgaddr" },
	{ 0x013, "ebc0_cfgdata" },
	{ 0x014, "dcp0_cfgaddr" },
	{ 0x015, "dcp0_cfgdata" },
	{ 0x018, "ocm0_isarc" },
	{ 0x019, "ocm0_iscntl" },
	{ 0x01a, "ocm0_dsarc" },
	{ 0x01b, "ocm0_dscntl" },
	{ 0x084, "plb0_besr" },
	{ 0x086, "plb0_bear" },
	{ 0x087, "plb0_acr" },
	{ 0x0a0, "pob0_besr0" },
	{ 0x0a2, "pob0_bear" },
	{ 0x0a4, "pob0_besr1" },
	{ 0x0b0, "cpc0_pllmr" },
	{ 0x0b1, "cpc0_cr0" },
	{ 0x0b2, "cpc0_cr1" },
	{ 0x0b4, "cpc0_psr" },
	{ 0x0b5, "cpc0_jtagid" },
	{ 0x0b8, "cpc0_sr" },
	{ 0x0b9, "cpc0_er" },
	{ 0x0ba, "cpc0_fr" },
	{ 0x0c0, "uic0_sr" },
	{ 0x0c2, "uic0_er" },
	{ 0x0c3, "uic0_cr" },
	{ 0x0c4, "uic0_pr" },
	{ 0x0c5, "uic0_tr" },
	{ 0x0c6, "uic0_msr" },
	{ 0x0c7, "uic0_vr" },
	{ 0x0c8, "uic0_vcr" },
	{ 0x100, "dma0_cr0" },
	{ 0x101, "dma0_ct0" },
	{ 0x102, "dma0_da0" },
	{ 0x103, "dma0_sa0" },
	{ 0x104, "dma0_sg0" },
	{ 0x108, "dma0_cr1" },
	{ 0x109, "dma0_ct1" },
	{ 0x10a, "dma0_da1" },
	{ 0x10b, "dma0_sa1" },
	{ 0x10c, "dma0_sg1" },
	{ 0x110, "dma0_cr2" },
	{ 0x111, "dma0_ct2" },
	{ 0x112, "dma0_da2" },
	{ 0x113, "dma0_sa2" },
	{ 0x114, "dma0_sg2" },
	{ 0x118, "dma0_cr3" },
	{ 0x119, "dma0_ct3" },
	{ 0x11a, "dma0_da3" },
	{ 0x11b, "dma0_sa3" },
	{ 0x11c, "dma0_sg3" },
	{ 0x120, "dma0_sr" },
	{ 0x123, "dma0_sgc" },
	{ 0x125, "dma0_slp" },
	{ 0x126, "dma0_pol" },
	{ 0x180, "mal0_cfg" },
	{ 0x181, "mal0_esr" },
	{ 0x182, "mal0_ier" },
	{ 0x184, "mal0_txcasr" },
	{ 0x185, "mal0_txcarr" },
	{ 0x186, "mal0_txeobisr" },
	{ 0x187, "mal0_txdeir" },
	{ 0x190, "mal0_rxcasr" },
	{ 0x191, "mal0_rxcarr" },
	{ 0x192, "mal0_rxeobisr" },
	{ 0x193, "mal0_rxdeir" },
	{ 0x1a0, "mal0_txctp0r" },
	{ 0x1a1, "mal0_txctp1r" },
	{ 0x1a2, "mal0_txctp2r" },
	{ 0x1a3, "mal0_txctp3r" },
	{ 0x1c0, "mal0_rxctp0r" },
	{ 0x1e0, "mal0_rcbs0" },
	{ 0, NULL }
};

static const char *condstr[8] = {
	"ge", "le", "ne", "ns", "lt", "gt", "eq", "so"
};


void
op_ill(instr_t instr, vm_offset_t loc)
{
	db_printf("illegal instruction %x\n", instr);
}

u_int32_t
extract_field(u_int32_t value, u_int32_t base, u_int32_t width)
{
	u_int32_t mask = (1 << width) - 1;
	return ((value >> base) & mask);
}

const struct opcode * search_op(const struct opcode *);

void
disasm_fields(const struct opcode *popcode, instr_t instr, vm_offset_t loc, 
	char *disasm_str, size_t slen)
{
	char * pstr;
	enum function_mask func;
	int len;

#define ADD_LEN(s)	do { \
		len = (s); \
		slen -= len; \
		pstr += len; \
	} while(0)
#define APP_PSTR(fmt, arg)	ADD_LEN(snprintf(pstr, slen, (fmt), (arg)))
#define APP_PSTRS(fmt)		ADD_LEN(snprintf(pstr, slen, "%s", (fmt)))

	pstr = disasm_str;

	func =  popcode->func;
	if (func & Op_BC) {
		u_int BO, BI;
		BO = extract_field(instr, 31 - 10, 5);
		BI = extract_field(instr, 31 - 15, 5);
		func &= ~Op_BC;
		if (BO & 4) {
			/* standard, no decrement */
			if (BO & 16) {
				if (popcode->code == 0x40000000) {
					APP_PSTRS("c");
					func |= Op_BO | Op_BI;
				}
			}
			else {
				APP_PSTRS(condstr[((BO & 8) >> 1) + (BI & 3)]);
				if (BI >= 4)
					func |= Op_crfS;
			}
		}
		else {
			/* decrement and branch */
			if (BO & 2)
				APP_PSTRS("dz");
			else
				APP_PSTRS("dnz");
			if ((BO & 24) == 0)
				APP_PSTRS("f");
			else if ((BO & 24) == 8)
				APP_PSTRS("t");
			else
				func |= Op_BI;
		}
		if (popcode->code == 0x4c000020)
			APP_PSTRS("lr");
		else if (popcode->code == 0x4c000420)
			APP_PSTRS("ctr");
		if ((BO & 20) != 20 && (func & Op_BO) == 0)
			func |= Op_BP;  /* branch prediction hint */
	}
	if (func & Op_OE) {
		u_int OE;
		OE = extract_field(instr, 31 - 21, 1);
		if (OE) {
			APP_PSTRS("o");
		}
		func &= ~Op_OE;
	}
	switch (func & Op_LKM) {
	case Op_Rc:
		if (instr & 0x1)
			APP_PSTRS(".");
		break;
	case Op_AA:
		if (instr & 0x1)
			APP_PSTRS("l");
		if (instr & 0x2) {
			APP_PSTRS("a");
			loc = 0; /* Absolute address */
		}
		break;
	case Op_LK:
		if (instr & 0x1)
			APP_PSTRS("l");
		break;
	default:
		func &= ~Op_LKM;
	}
	if (func & Op_BP) {
		int y;
		y = (instr & 0x200000) != 0;
		if (popcode->code == 0x40000000) {
			int BD;
			BD = extract_field(instr, 31 - 29, 14);
			BD = BD << 18;
			BD = BD >> 16;
			BD += loc;
			if ((vm_offset_t)BD < loc)
				y ^= 1;
		}
		APP_PSTR("%c", y ? '+' : '-');
		func &= ~Op_BP;
	}
	APP_PSTRS("\t");

	/* XXX: special cases here, out of flags in a 32bit word. */
	if (strcmp(popcode->name, "wrteei") == 0) {
		int E;
		E = extract_field(instr, 31 - 16, 5);
		APP_PSTR("%d", E);
		return;
	}
	else if (strcmp(popcode->name, "mtfsfi") == 0) {
		u_int UI;
		UI = extract_field(instr, 31 - 8, 3);
		APP_PSTR("crf%u, ", UI);
		UI = extract_field(instr, 31 - 19, 4);
		APP_PSTR("0x%x", UI);
	}
	/* XXX: end of special cases here. */

	if ((func & Op_FM) == Op_FM) {
		u_int FM;
		FM = extract_field(instr, 31 - 14, 8);
		APP_PSTR("0x%x, ", FM);
		func &= ~Op_FM;
	}
	if (func & Op_D) {  /* Op_ST is the same */
		u_int D;
		D = extract_field(instr, 31 - 10, 5);
		APP_PSTR("r%d, ", D);
		func &= ~Op_D;
	}
	if (func & Op_crbD) {
		u_int crbD;
		crbD = extract_field(instr, 31 - 10, 5);
		APP_PSTR("crb%d, ", crbD);
		func &= ~Op_crbD;
	}
	if (func & Op_crfD) {
		u_int crfD;
		crfD = extract_field(instr, 31 - 8, 3);
		APP_PSTR("crf%d, ", crfD);
		func &= ~Op_crfD;
	}
	if (func & Op_TO) {
		u_int TO;
		TO = extract_field(instr, 31 - 10, 1);
		APP_PSTR("%d, ", TO);
		func &= ~Op_TO;
	}
	if (func & Op_crfS) {
		u_int crfS;
		crfS = extract_field(instr, 31 - 13, 3);
		APP_PSTR("crf%d, ", crfS);
		func &= ~Op_crfS;
	}
	if (func & Op_CRM) {
		u_int CRM;
		CRM = extract_field(instr, 31 - 19, 8);
		APP_PSTR("0x%x, ", CRM);
		func &= ~Op_CRM;
	}
	if (func & Op_BO) {
		u_int BO;
		BO = extract_field(instr, 31 - 10, 5);
		APP_PSTR("%d, ", BO);
		func &= ~Op_BO;
	}
	if (func & Op_BI) {
		u_int BI;
		BI = extract_field(instr, 31 - 15, 5);
		APP_PSTR("%d, ", BI);
		func &= ~Op_BI;
	}
	if (func & Op_dA) {  /* register A indirect with displacement */
		u_int A;
		A = extract_field(instr, 31 - 31, 16);
		if (A & 0x8000) {
			APP_PSTRS("-");
			A = 0x10000-A;
		}
		APP_PSTR("0x%x", A);
		A = extract_field(instr, 31 - 15, 5);
		APP_PSTR("(r%d)", A);
		func &= ~Op_dA;
	}
	if (func & Op_spr) {
		u_int spr;
		u_int sprl;
		u_int sprh;
		const struct specialreg *regs;
		int i;
		sprl = extract_field(instr, 31 - 15, 5);
		sprh = extract_field(instr, 31 - 20, 5);
		spr = sprh << 5 | sprl;

		/* ugly hack - out of bitfields in the function mask */
		if (popcode->name[2] == 'd')	/* m.Dcr */
			regs = dcrregs;
		else
			regs = sprregs;
		for (i = 0; regs[i].name != NULL; i++)
			if (spr == regs[i].reg)
				break;
		if (regs[i].name == NULL)
			APP_PSTR("[unknown special reg (%d)]", spr);
		else
			APP_PSTR("%s", regs[i].name);

		if (popcode->name[1] == 't')	/* spr is destination */
			APP_PSTRS(", ");
		func &= ~Op_spr;
	}
	if (func & Op_SR) {
		u_int SR;
		SR = extract_field(instr, 31 - 15, 3);
		APP_PSTR("sr%d", SR);
		if (popcode->name[1] == 't')	/* SR is destination */
			APP_PSTRS(", ");
		func &= ~Op_SR;
	}
	if (func & Op_A) {
		u_int A;
		A = extract_field(instr, 31 - 15, 5);
		APP_PSTR("r%d, ", A);
		func &= ~Op_A;
	}
	if (func & Op_S) {
		u_int D;
		D = extract_field(instr, 31 - 10, 5);
		APP_PSTR("r%d, ", D);
		func &= ~Op_S;
	}
	if (func & Op_C) {
		u_int C;
		C = extract_field(instr, 31 - 25, 5);
		APP_PSTR("r%d, ", C);
		func &= ~Op_C;
	}
	if (func & Op_B) {
		u_int B;
		B = extract_field(instr, 31 - 20, 5);
		APP_PSTR("r%d", B);
		func &= ~Op_B;
	}
	if (func & Op_crbA) {
		u_int crbA;
		crbA = extract_field(instr, 31 - 15, 5);
		APP_PSTR("%d, ", crbA);
		func &= ~Op_crbA;
	}
	if (func & Op_crbB) {
		u_int crbB;
		crbB = extract_field(instr, 31 - 20, 5);
		APP_PSTR("%d, ", crbB);
		func &= ~Op_crbB;
	}
	if (func & Op_LI) {
		int LI;
		LI = extract_field(instr, 31 - 29, 24);
		LI = LI << 8;
		LI = LI >> 6;
		LI += loc;
		APP_PSTR("0x%x", LI);
		func &= ~Op_LI;
	}
	switch (func & Op_SIMM) {
		u_int IMM;
	case Op_SIMM: /* same as Op_d */
		IMM = extract_field(instr, 31 - 31, 16);
		if (IMM & 0x8000) {
			APP_PSTRS("-");
			IMM = 0x10000-IMM;
		}
		func &= ~Op_SIMM;
		goto common;
	case Op_UIMM:
		IMM = extract_field(instr, 31 - 31, 16);
		func &= ~Op_UIMM;
		goto common;
	common:
		APP_PSTR("0x%x", IMM);
		break;
	default:
		;
	}
	if (func & Op_BD) {
		int BD;
		BD = extract_field(instr, 31 - 29, 14);
		BD = BD << 18;
		BD = BD >> 16;
		BD += loc;
		/* Need to sign extend and shift up 2, then add addr */
		APP_PSTR("0x%x", BD);
		func &= ~Op_BD;
	}
	if (func & Op_ds) {
		u_int ds;
		ds = extract_field(instr, 31 - 29, 14) << 2;
		APP_PSTR("0x%x", ds);
		func &= ~Op_ds;
	}
	if (func & Op_me) {
		u_int me, mel, meh;
		mel = extract_field(instr, 31 - 25, 4);
		meh = extract_field(instr, 31 - 26, 1);
		me = meh << 4 | mel;
		APP_PSTR(", 0x%x", me);
		func &= ~Op_me;
	}
	if ((func & Op_SH) && (func & Op_sh_mb_sh)) {
		u_int SH;
		SH = extract_field(instr, 31 - 20, 5);
		APP_PSTR("%d", SH);
	}
	if ((func & Op_MB) && (func & Op_sh_mb_sh)) {
		u_int MB;
		u_int ME;
		MB = extract_field(instr, 31 - 25, 5);
		APP_PSTR(", %d", MB);
		ME = extract_field(instr, 31 - 30, 5);
		APP_PSTR(", %d", ME);
	}
	if ((func & Op_sh) && ! (func & Op_sh_mb_sh)) {
		u_int sh, shl, shh;
		shl = extract_field(instr, 31 - 19, 4);
		shh = extract_field(instr, 31 - 20, 1);
		sh = shh << 4 | shl;
		APP_PSTR(", %d", sh);
	}
	if ((func & Op_mb) && ! (func & Op_sh_mb_sh)) {
		u_int mb, mbl, mbh;
		mbl = extract_field(instr, 31 - 25, 4);
		mbh = extract_field(instr, 31 - 26, 1);
		mb = mbh << 4 | mbl;
		APP_PSTR(", %d", mb);
	}
	if ((func & Op_me) && ! (func & Op_sh_mb_sh)) {
		u_int me, mel, meh;
		mel = extract_field(instr, 31 - 25, 4);
		meh = extract_field(instr, 31 - 26, 1);
		me = meh << 4 | mel;
		APP_PSTR(", %d", me);
	}
	if (func & Op_tbr) {
		u_int tbr;
		u_int tbrl;
		u_int tbrh;
		const char *reg;
		tbrl = extract_field(instr, 31 - 15, 5);
		tbrh = extract_field(instr, 31 - 20, 5);
		tbr = tbrh << 5 | tbrl;

		switch (tbr) {
		case 268:
			reg = "tbl";
			break;
		case 269:
			reg = "tbu";
			break;
		default:
			reg = NULL;
		}
		if (reg == NULL)
			APP_PSTR(", [unknown tbr %d ]", tbr);
		else
			APP_PSTR(", %s", reg);
		func &= ~Op_tbr;
	}
	if (func & Op_NB) {
		u_int NB;
		NB = extract_field(instr, 31 - 20, 5);
		if (NB == 0)
			NB = 32;
		APP_PSTR(", %d", NB);
		func &= ~Op_SR;
	}
#undef ADD_LEN
#undef APP_PSTR
#undef APP_PSTRS
}

void
op_base(instr_t instr, vm_offset_t loc)
{
	dis_ppc(opcodes, instr, loc);
}

void
op_cl_x13(instr_t instr, vm_offset_t loc)
{
	dis_ppc(opcodes_13, instr, loc);
}

void
op_cl_x1e(instr_t instr, vm_offset_t loc)
{
	dis_ppc(opcodes_1e, instr, loc);
}

void
op_cl_x1f(instr_t instr, vm_offset_t loc)
{
	dis_ppc(opcodes_1f, instr, loc);
}

void
op_cl_x3a(instr_t instr, vm_offset_t loc)
{
	dis_ppc(opcodes_3a, instr, loc);
}

void
op_cl_x3b(instr_t instr, vm_offset_t loc)
{
	dis_ppc(opcodes_3b, instr, loc);
}

void
op_cl_x3e(instr_t instr, vm_offset_t loc)
{
	dis_ppc(opcodes_3e, instr, loc);
}

void
op_cl_x3f(instr_t instr, vm_offset_t loc)
{
	dis_ppc(opcodes_3f, instr, loc);
}

void
dis_ppc(const struct opcode *opcodeset, instr_t instr, vm_offset_t loc)
{
	const struct opcode *op;
	int found = 0;
	int i;
	char disasm_str[80];

	for (i = 0, op = &opcodeset[0];
	    found == 0 && op->mask != 0;
	    i++, op = &opcodeset[i]) {
		if ((instr & op->mask) == op->code) {
			found = 1;
			disasm_fields(op, instr, loc, disasm_str,
				sizeof disasm_str);
			db_printf("%s%s\n", op->name, disasm_str);
			return;
		}
	}
	op_ill(instr, loc);
}

db_addr_t
db_disasm(db_addr_t loc, bool extended)
{
	int class;
	instr_t opcode;
	opcode = *(instr_t *)(loc);
	class = opcode >> 26;
	(opcodes_base[class])(opcode, loc);

	return (loc + 4);
}

vm_offset_t opc_disasm(vm_offset_t loc, int);

vm_offset_t
opc_disasm(vm_offset_t loc, int xin)
{
	int class;
	instr_t opcode;
	opcode = xin;
	class = opcode >> 26;
	(opcodes_base[class])(opcode, loc);

	return (loc + 4);
}
