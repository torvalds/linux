/*	$OpenBSD: db_disasm.c,v 1.5 2024/11/07 16:02:29 miod Exp $	*/
/*
 * Copyright (c) 1996, 2001, 2003 Dale Rahn. All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>
#include <ddb/db_interface.h>
#include <ddb/db_output.h>

enum opf {
	Opf_INVALID,
	Opf_A,
	Opf_A0,
	Opf_B,
	Opf_BI,
	Opf_BI1,
	Opf_BO,
	Opf_CRM,
	Opf_D,
	Opf_S,
	Opf_FM,
	Opf_LK,
	Opf_RC,
	Opf_AA,
	Opf_LI,
	Opf_OE,
	Opf_SR,
	Opf_TO,
	Opf_SIMM,
	Opf_UIMM,
	Opf_crbA,
	Opf_crbB,
	Opf_crbD,
	Opf_crfD,
	Opf_crfS,
	Opf_d,
	Opf_ds,
	Opf_spr,
	Opf_tbr,

	Opf_BD,
	Opf_C,

	Opf_NB,

	Opf_sh,
	Opf_SH,
	Opf_mb,
	Opf_MB,
	Opf_ME,
};


struct db_field {
	char *name;
	enum opf opf;
} db_fields[] = {
	{ "A",		Opf_A },
	{ "A0",		Opf_A0 },
	{ "B",		Opf_B },
	{ "D",		Opf_D },
	{ "S",		Opf_S },
	{ "AA",		Opf_AA },
	{ "LI",		Opf_LI },
	{ "BD",		Opf_BD },
	{ "BI",		Opf_BI },
	{ "BI1",	Opf_BI1 },
	{ "BO",		Opf_BO },
	{ "CRM",	Opf_CRM },
	{ "FM",		Opf_FM },
	{ "LK",		Opf_LK },
	{ "MB",		Opf_MB },
	{ "ME",		Opf_ME },
	{ "NB",		Opf_NB },
	{ "OE",		Opf_OE },
	{ "RC",		Opf_RC },
	{ "SH",		Opf_SH },
	{ "SR",		Opf_SR },
	{ "TO",		Opf_TO },
	{ "SIMM",	Opf_SIMM },
	{ "UIMM",	Opf_UIMM },
	{ "crbA",	Opf_crbA },
	{ "crbB",	Opf_crbB },
	{ "crbD",	Opf_crbD },
	{ "crfD",	Opf_crfD },
	{ "crfS",	Opf_crfS },
	{ "d",		Opf_d },
	{ "ds",		Opf_ds },
	{ "mb",		Opf_mb },
	{ "sh",		Opf_sh },
	{ "spr",	Opf_spr },
	{ "tbr",	Opf_tbr },
	{ NULL,		0 }
};

struct opcode {
	char *name;
	u_int32_t mask;
	u_int32_t code;
	char *decode_str;
};

typedef u_int32_t instr_t;
typedef void (op_class_func) (u_int32_t addr, instr_t instr);

u_int32_t extract_field(u_int32_t value, u_int32_t base, u_int32_t width);
void disasm_fields(u_int32_t addr, const struct opcode *popcode, instr_t instr,
    char *disasm_str, size_t bufsize);
void disasm_process_field(u_int32_t addr, instr_t instr, char **ppfmt,
    char *ppoutput, size_t bufsize);
void dis_ppc(u_int32_t addr, const struct opcode *opcodeset, instr_t instr);


op_class_func op_ill, op_base;
op_class_func op_cl_x13, op_cl_x1e, op_cl_x1f;
op_class_func op_cl_x3a, op_cl_x3b;
op_class_func op_cl_x3e, op_cl_x3f;

op_class_func *opcodes_base[] = {
/*x00*/	op_ill,		op_ill,		op_base,	op_ill,
/*x04*/	op_ill,		op_ill,		op_ill,		op_base,
/*x08*/	op_base,	op_base,	op_base,	op_base,
/*x0C*/	op_base,	op_base,	op_base/*XXX*/,	op_base/*XXX*/,
/*x10*/	op_base,	op_base,	op_base,	op_cl_x13,
/*x14*/	op_base,	op_base,	op_ill,		op_base,
/*x18*/	op_base,	op_base,	op_base,	op_base,
/*x1C*/	op_base,	op_base,	op_cl_x1e,	op_cl_x1f,
/*x20*/	op_base,	op_base,	op_base,	op_base,
/*x24*/	op_base,	op_base,	op_base,	op_base,
/*x28*/	op_base,	op_base,	op_base,	op_base,
/*x2C*/	op_base,	op_base,	op_base,	op_base,
/*x30*/	op_base,	op_base,	op_base,	op_base,
/*x34*/	op_base,	op_base,	op_base,	op_base,
/*x38*/	op_ill,		op_ill,		op_cl_x3a,	op_cl_x3b,
/*x3C*/	op_ill,		op_ill,		op_cl_x3e,	op_cl_x3f
};


/* This table could be modified to make significant the "reserved" fields
 * of the opcodes, But I didn't feel like it when typing in the table,
 * I would recommend that this table be looked over for errors,
 * This was derived from the table in Appendix A.2 of (Mot part # MPCFPE/AD)
 * PowerPC Microprocessor Family: The Programming Environments
 */
	
const struct opcode opcodes[] = {
	{ "tdi",	0xfc000000, 0x08000000, " %{TO},%{A},%{SIMM}" },
	{ "twi",	0xfc000000, 0x0c000000, " %{TO},%{A},%{SIMM}" },

	{ "mulli",	0xfc000000, 0x1c000000, " %{D},%{A},%{SIMM}" },
	{ "subfic",	0xfc000000, 0x20000000, " %{D},%{A},%{SIMM}" },
	{ "cmpli",	0xff800000, 0x28000000, " %{A},%{UIMM}" },
	{ "cmpli",	0xfc400000, 0x28000000, " %{crfD}%{A}, %{UIMM}" },
	{ "cmpi",	0xff800000, 0x2c000000, " %{A},%{SIMM}"},
	{ "cmpi",	0xfc400000, 0x2c000000, " %{crfD}%{A},%{SIMM}" },
	{ "addic",	0xfc000000, 0x30000000, " %{D},%{A},%{SIMM}" },
	{ "addic.",	0xfc000000, 0x34000000, " %{D},%{A},%{SIMM}" },
	{ "addi",	0xfc000000, 0x38000000, " %{D},%{A0}%{SIMM}" },
	{ "addis",	0xfc000000, 0x3c000000, " %{D},%{A0}%{SIMM}" },
	{ "sc",		0xffffffff, 0x44000002, "" },
	{ "b",		0xfc000000, 0x40000000, "%{BO}%{LK}%{AA} %{BI}%{BD}" },
	{ "b",		0xfc000000, 0x48000000, "%{LK}%{AA} %{LI}" },

	{ "rlwimi",	0xfc000000, 0x50000000, "%{RC} %{A},%{S},%{SH},%{MB},%{ME}" },
	{ "rlwinm",	0xfc000000, 0x54000000, "%{RC} %{A},%{S},%{SH},%{MB},%{ME}" },
	{ "rlwnm",	0xfc000000, 0x5c000000, "%{RC} %{A},%{S},%{SH},%{MB},%{ME}" },

	{ "ori",	0xfc000000, 0x60000000, " %{A},%{S},%{UIMM}" },
	{ "oris",	0xfc000000, 0x64000000, " %{A},%{S},%{UIMM}" },
	{ "xori",	0xfc000000, 0x68000000, " %{A},%{S},%{UIMM}" },
	{ "xoris",	0xfc000000, 0x6c000000, " %{A},%{S},%{UIMM}" },

	{ "andi.",	0xfc000000, 0x70000000, " %{A},%{S},%{UIMM}" },
	{ "andis.",	0xfc000000, 0x74000000, " %{A},%{S},%{UIMM}" },

	{ "lwz",	0xfc000000, 0x80000000, " %{D},%{d}(%{A})" },
	{ "lwzu",	0xfc000000, 0x84000000, " %{D},%{d}(%{A})" },
	{ "lbz",	0xfc000000, 0x88000000, " %{D},%{d}(%{A})" },
	{ "lbzu",	0xfc000000, 0x8c000000, " %{D},%{d}(%{A})" },
	{ "stw",	0xfc000000, 0x90000000, " %{S},%{d}(%{A})" },
	{ "stwu",	0xfc000000, 0x94000000, " %{S},%{d}(%{A})" },
	{ "stb",	0xfc000000, 0x98000000, " %{S},%{d}(%{A})" },
	{ "stbu",	0xfc000000, 0x9c000000, " %{S},%{d}(%{A})" },

	{ "lhz",	0xfc000000, 0xa0000000, " %{D},%{d}(%{A})" },
	{ "lhzu",	0xfc000000, 0xa4000000, " %{D},%{d}(%{A})" },
	{ "lha",	0xfc000000, 0xa8000000, " %{D},%{d}(%{A})" },
	{ "lhau",	0xfc000000, 0xac000000, " %{D},%{d}(%{A})" },
	{ "sth",	0xfc000000, 0xb0000000, " %{S},%{d}(%{A})" },
	{ "sthu",	0xfc000000, 0xb4000000, " %{S},%{d}(%{A})" },
	{ "lmw",	0xfc000000, 0xb8000000, " %{D},%{d}(%{A})" },
	{ "stmw",	0xfc000000, 0xbc000000, " %{S},%{d}(%{A})" },

	{ "lfs",	0xfc000000, 0xc0000000, " %{D},%{d}(%{A})" },
	{ "lfsu",	0xfc000000, 0xc4000000, " %{D},%{d}(%{A})" },
	{ "lfd",	0xfc000000, 0xc8000000, " %{D},%{d}(%{A})" },
	{ "lfdu",	0xfc000000, 0xcc000000, " %{D},%{d}(%{A})" },

	{ "stfs",	0xfc000000, 0xd0000000, " %{S},%{d}(%{A})" },
	{ "stfsu",	0xfc000000, 0xd4000000, " %{S},%{d}(%{A})" },
	{ "stfd",	0xfc000000, 0xd8000000, " %{S},%{d}(%{A})" },
	{ "stfdu",	0xfc000000, 0xdc000000, " %{S},%{d}(%{A})" },
	{ "",		0x0,		0x0, "" }

};

/* 13 * 4 = 4c */
const struct opcode opcodes_13[] = {
/* 0x13 << 2 */
	{ "mcrf",	0xfc0007fe, 0x4c000000, " %{crfD},%{crfS}" },
	{ "b",/*bclr*/	0xfc0007fe, 0x4c000020, "%{BO}lr%{LK} %{BI1}" },
	{ "crnor",	0xfc0007fe, 0x4c000042, " %{crbD},%{crbA},%{crbB}" },
	{ "rfi",	0xfc0007fe, 0x4c000064, "" },
	{ "crandc",	0xfc0007fe, 0x4c000102, " %{crbD},%{crbA},%{crbB}" },
	{ "isync",	0xfc0007fe, 0x4c00012c, "" },
	{ "crxor",	0xfc0007fe, 0x4c000182, " %{crbD},%{crbA},%{crbB}" },
	{ "crnand",	0xfc0007fe, 0x4c0001c2, " %{crbD},%{crbA},%{crbB}" },
	{ "crand",	0xfc0007fe, 0x4c000202, " %{crbD},%{crbA},%{crbB}" },
	{ "creqv",	0xfc0007fe, 0x4c000242, " %{crbD},%{crbA},%{crbB}" },
	{ "crorc",	0xfc0007fe, 0x4c000342, " %{crbD},%{crbA},%{crbB}" },
	{ "cror",	0xfc0007fe, 0x4c000382, " %{crbD},%{crbA},%{crbB}" },
	{ "b"/*bcctr*/,	0xfc0007fe, 0x4c000420, "%{BO}ctr%{LK} %{BI1}" },
	{ "",		0x0,		0x0, "" }
};

/* 1e * 4 = 78 */
const struct opcode opcodes_1e[] = {
	{ "rldicl",	0xfc00001c, 0x78000000, " %{A},%{S},%{sh},%{mb}" },
	{ "rldicr",	0xfc00001c, 0x78000004, " %{A},%{S},%{sh},%{mb}" },
	{ "rldic",	0xfc00001c, 0x78000008, " %{A},%{S},%{sh},%{mb}" },
	{ "rldimi",	0xfc00001c, 0x7800000c, " %{A},%{S},%{sh},%{mb}" },
	{ "rldcl",	0xfc00003e, 0x78000010, " %{A},%{S},%{B},%{mb}" },
	{ "rldcr",	0xfc00003e, 0x78000012, " %{A},%{S},%{B},%{mb}" },
	{ "",		0x0,		0x0, "" }
};

/* 1f * 4 = 7c */
const struct opcode opcodes_1f[] = {
/* 1f << 2 */
	{ "cmpd",	0xfc2007fe, 0x7c200000, " %{crfD}%{A},%{B}" },
	{ "cmpw",	0xfc2007fe, 0x7c000000, " %{crfD}%{A},%{B}" },
	{ "tw",		0xfc0007fe, 0x7c000008, " %{TO},%{A},%{B}" },
	{ "subfc",	0xfc0003fe, 0x7c000010, "%{OE}%{RC} %{D},%{A},%{B}" },
	{ "mulhdu",	0xfc0007fe, 0x7c000012, "%{RC} %{D},%{A},%{B}" },
	{ "addc",	0xfc0003fe, 0x7c000014, "%{OE}%{RC} %{D},%{A},%{B}" },
	{ "mulhwu",	0xfc0007fe, 0x7c000016, "%{RC} %{D},%{A},%{B}" },

	{ "mfcr",	0xfc0007fe, 0x7c000026, " %{D}" },
	{ "lwarx",	0xfc0007fe, 0x7c000028, " %{D},%{A0}%{B}" },
	{ "ldx",	0xfc0007fe, 0x7c00002a, " %{D},%{A0}%{B}" },
	{ "lwzx",	0xfc0007fe, 0x7c00002e, " %{D},%{A0}%{B}" },
	{ "slw",	0xfc0007fe, 0x7c000030, "%{RC} %{A},%{S},%{B}" },
	{ "cntlzw",	0xfc0007fe, 0x7c000034, "%{RC} %{A},%{S}" },
	{ "sld",	0xfc0007fe, 0x7c000036, "%{RC} %{A},%{S},%{B}" },
	{ "and",	0xfc0007fe, 0x7c000038, "%{RC} %{A},%{S},%{B}" },
	{ "cmpld",	0xfc2007fe, 0x7c200040, " %{crfD}%{A},%{B}" },
	{ "cmplw",	0xfc2007fe, 0x7c000040, " %{crfD}%{A},%{B}" },
	{ "subf",	0xfc0003fe, 0x7c000050, "%{OE}%{RC} %{D},%{A},%{B}" },
	{ "ldux",	0xfc0007fe, 0x7c00006a, " %{D},%{A},%{B}" },
	{ "dcbst",	0xfc0007fe, 0x7c00006c, " %{A0}%{B}" },
	{ "lwzux",	0xfc0007fe, 0x7c00006e, " %{D},%{A},%{B}" },
	{ "cntlzd",	0xfc0007fe, 0x7c000074, "%{RC} %{A},%{S}" },
	{ "andc",	0xfc0007fe, 0x7c000078, "%{RC} %{A},%{S},%{B}" },
	{ "td",		0xfc0007fe, 0x7c000088, " %{TO},%{A},%{B}" },
	{ "mulhd",	0xfc0007fe, 0x7c000092, "%{RC} %{D},%{A},%{B}" },
	{ "mulhw",	0xfc0007fe, 0x7c000096, "%{RC} %{D},%{A},%{B}" },
	{ "mfmsr",	0xfc0007fe, 0x7c0000a6, " %{D}" },
	{ "ldarx",	0xfc0007fe, 0x7c0000a8, " %{D},%{A0}%{B}" },
	{ "dcbf",	0xfc0007fe, 0x7c0000ac, " %{A0}%{B}" },
	{ "lbzx",	0xfc0007fe, 0x7c0000ae, " %{D},%{A0}%{B}" },
	{ "neg",	0xfc0003fe, 0x7c0000d0, "%{OE}%{RC} %{D},%{A}" },
	{ "lbzux",	0xfc0007fe, 0x7c0000ee, " %{D},%{A},%{B}" },
	{ "nor",	0xfc0007fe, 0x7c0000f8, "%{RC} %{A},%{S}" },
	{ "subfe",	0xfc0003fe, 0x7c000110, "%{OE}%{RC} %{D},%{A}" },
	{ "adde",	0xfc0003fe, 0x7c000114, "%{OE}%{RC} %{D},%{A}" },
	{ "mtcrf",	0xfc0007fe, 0x7c000120, " %{S},%{CRM}" },
	{ "mtmsr",	0xfc0007fe, 0x7c000124, " %{S}" },
	{ "stdx",	0xfc0007fe, 0x7c00012a, " %{S},%{A0}%{B}" },
	{ "stwcx.",	0xfc0007ff, 0x7c00012d, " %{S},%{A},%{B}" },
	{ "stwx",	0xfc0007fe, 0x7c00012e, " %{S},%{A},%{B}" },
	{ "stdux",	0xfc0007fe, 0x7c00016a, " %{S},%{A},%{B}" },
	{ "stwux",	0xfc0007fe, 0x7c00016e, " %{S},%{A},%{B}" },
	{ "subfze",	0xfc0003fe, 0x7c000190, "%{OE}%{RC} %{D},%{A}" },
	{ "addze",	0xfc0003fe, 0x7c000194, "%{OE}%{RC} %{D},%{A}" },
	{ "mtsr",	0xfc0007fe, 0x7c0001a4, " %{SR},%{S}" },
	{ "stdcx.",	0xfc0007ff, 0x7c0001ad, " %{S},%{A0}%{B}" },
	{ "stbx",	0xfc0007fe, 0x7c0001ae, " %{S},%{A0}%{B}" },
	{ "subfme",	0xfc0003fe, 0x7c0001d0, "%{OE}%{RC} %{D},%{A}" },
	{ "mulld",	0xfc0003fe, 0x7c0001d2, "%{OE}%{RC} %{D},%{A},%{B}" },
	{ "addme",	0xfc0003fe, 0x7c0001d4, "%{OE}%{RC} %{D},%{A}" },
	{ "mullw",	0xfc0003fe, 0x7c0001d6, "%{OE}%{RC} %{D},%{A},%{B}" },
	{ "mtsrin",	0xfc0007fe, 0x7c0001e4, " %{S},%{B}" },
	{ "dcbtst",	0xfc0007fe, 0x7c0001ec, " %{A0}%{B}" },
	{ "stbux",	0xfc0007fe, 0x7c0001ee, " %{S},%{A},%{B}" },
	{ "add",	0xfc0003fe, 0x7c000214, "" },
	{ "dcbt",	0xfc0007fe, 0x7c00022c, " %{A0}%{B}" },
	{ "lhzx",	0xfc0007ff, 0x7c00022e, " %{D},%{A0}%{B}" },
	{ "eqv",	0xfc0007fe, 0x7c000238, "%{RC} %{A},%{S},%{B}" },
	{ "tlbie",	0xfc0007fe, 0x7c000264, " %{B}" },
	{ "eciwx",	0xfc0007fe, 0x7c00026c, " %{D},%{A0}%{B}" },
	{ "lhzux",	0xfc0007fe, 0x7c00026e, " %{D},%{A},%{B}" },
	{ "xor",	0xfc0007fe, 0x7c000278, "%{RC} %{A},%{S},%{B}" },
	{ "mfspr",	0xfc0007fe, 0x7c0002a6, " %{D},%{spr}" },
	{ "lwax",	0xfc0007fe, 0x7c0002aa, " %{D},%{A0}%{B}" },
	{ "lhax",	0xfc0007fe, 0x7c0002ae, " %{D},%{A},%{B}" },
	{ "tlbia",	0xfc0007fe, 0x7c0002e4, "" },
	{ "mftb",	0xfc0007fe, 0x7c0002e6, " %{D},%{tbr}" },
	{ "lwaux",	0xfc0007fe, 0x7c0002ea, " %{D},%{A},%{B}" },
	{ "lhaux",	0xfc0007fe, 0x7c0002ee, " %{D},%{A},%{B}" },
	{ "sthx",	0xfc0007fe, 0x7c00032e, " %{S},%{A0}%{B}" },
	{ "orc",	0xfc0007fe, 0x7c000338, "%{RC} %{A},%{S},%{B}" },
	{ "ecowx",	0xfc0007fe, 0x7c00036c, "%{RC} %{S},%{A0}%{B}" },
	{ "slbie",	0xfc0007fc, 0x7c000364, " %{B}" },
	{ "sthux",	0xfc0007fe, 0x7c00036e, " %{S},%{A0}%{B}" },
	{ "or",		0xfc0007fe, 0x7c000378, "%{RC} %{A},%{S},%{B}" },
	{ "divdu",	0xfc0003fe, 0x7c000392, "%{OE}%{RC} %{S},%{A},%{B}" },
	{ "divwu",	0xfc0003fe, 0x7c000396, "%{OE}%{RC} %{S},%{A},%{B}" },
	{ "mtspr",	0xfc0007fe, 0x7c0003a6, " %{spr},%{S}" },
	{ "dcbi",	0xfc0007fe, 0x7c0003ac, " %{A0}%{B}" },
	{ "nand",	0xfc0007fe, 0x7c0003b8, "%{RC} %{A},%{S},%{B}" },
	{ "divd",	0xfc0003fe, 0x7c0003d2, "%{OE}%{RC} %{S},%{A},%{B}" },
	{ "divw",	0xfc0003fe, 0x7c0003d6, "%{OE}%{RC} %{S},%{A},%{B}" },
	{ "slbia",	0xfc0003fe, 0x7c0003e4, "%{OE}%{RC} %{S},%{A},%{B}" },
	{ "mcrxr",	0xfc0007fe, 0x7c000400, "crfD1" },
	{ "lswx",	0xfc0007fe, 0x7c00042a, " %{D},%{A0}%{B}" },
	{ "lwbrx",	0xfc0007fe, 0x7c00042c, " %{D},%{A0}%{B}" },
	{ "lfsx",	0xfc0007fe, 0x7c00042e, " %{D},%{A},%{B}" },
	{ "srw",	0xfc0007fe, 0x7c000430, "%{RC} %{A},%{S},%{B}" },
	{ "srd",	0xfc0007fe, 0x7c000436, "%{RC} %{A},%{S},%{B}" },
	{ "tlbsync",	0xffffffff, 0x7c00046c, "" },
	{ "lfsux",	0xfc0007fe, 0x7c00046e, " %{D},%{A},%{B}" },
	{ "mfsr",	0xfc0007fe, 0x7c0004a6, " %{D},%{SR}" },
	{ "lswi",	0xfc0007fe, 0x7c0004aa, " %{D},%{A},%{NB}" },
	{ "sync",	0xfc0007fe, 0x7c0004ac, "" },
	{ "lfdx",	0xfc0007fe, 0x7c0004ae, " %{D},%{A},%{B}" },
	{ "lfdux",	0xfc0007fe, 0x7c0004ee, " %{D},%{A},%{B}" },
	{ "mfsrin",	0xfc0007fe, 0x7c000526, "" },
	{ "stswx",	0xfc0007fe, 0x7c00052a, " %{S},%{A0}%{B}" },
	{ "stwbrx",	0xfc0007fe, 0x7c00052c, " %{S},%{A0}%{B}" },
	{ "stfsx",	0xfc0007fe, 0x7c00052e, " %{S},%{A0}%{B}" },
	{ "stfsux",	0xfc0007fe, 0x7c00056e, " %{S},%{A},%{B}" },
	{ "stswi",	0xfc0007fe, 0x7c0005aa, "%{S},%{A0}%{NB}" },
	{ "stfdx",	0xfc0007fe, 0x7c0005ae, " %{S},%{A0}%{B}" },
	{ "stfdux",	0xfc0007fe, 0x7c0005ee, " %{S},%{A},%{B}" },
	{ "lhbrx",	0xfc0007fe, 0x7c00062c, " %{D},%{A0}%{B}" },
	{ "sraw",	0xfc0007fe, 0x7c000630, " %{A},%{S},%{B}" },
	{ "srad",	0xfc0007fe, 0x7c000634, "%{RC} %{A},%{S},%{B}" },
	{ "srawi",	0xfc0007fe, 0x7c000670, "%{RC} %{A},%{SH}" },
	{ "sradi",	0xfc0007fc, 0x7c000674, " %{A},%{S},%{sh}" },
	{ "eieio",	0xfc0007fe, 0x7c0006ac, "" }, /* MASK? */
	{ "sthbrx",	0xfc0007fe, 0x7c00072c, " %{S},%{A0}%{B}" },
	{ "extsh",	0xfc0007fe, 0x7c000734, "%{RC} %{A},%{S}" },
	{ "extsb",	0xfc0007fe, 0x7c000774, "%{RC} %{A},%{S}" },
	{ "icbi",	0xfc0007fe, 0x7c0007ac, " %{A0}%{B}" },

	{ "stfiwx",	0xfc0007fe, 0x7c0007ae, " %{S},%{A0}%{B}" },
	{ "extsw",	0xfc0007fe, 0x7c0007b4, "%{RC} %{A},%{S}" },
	{ "dcbz",	0xfc0007fe, 0x7c0007ec, " %{A0}%{B}" },
	{ "",		0x0,		0x0, 0, }
};

/* 3a * 4 = e8 */
const struct opcode opcodes_3a[] = {
	{ "ld",		0xfc000003, 0xe8000000, " %{D},%{ds}(%{A})" },
	{ "ldu",	0xfc000003, 0xe8000001, " %{D},%{ds}(%{A})" },
	{ "lwa",	0xfc000003, 0xe8000002, " %{D},%{ds}(%{A})" },
	{ "",		0x0,		0x0, "" }
};

/* 3b * 4 = ec */
const struct opcode opcodes_3b[] = {
	{ "fdivs",	0xfc00003e, 0xec000024, "%{RC} f%{D},f%{A},f%{B}" },
	{ "fsubs",	0xfc00003e, 0xec000028, "%{RC} f%{D},f%{A},f%{B}" },

	{ "fadds",	0xfc00003e, 0xec00002a, "%{RC} f%{D},f%{A},f%{B}" },
	{ "fsqrts",	0xfc00003e, 0xec00002c, "" },
	{ "fres",	0xfc00003e, 0xec000030, "" },
	{ "fmuls",	0xfc00003e, 0xec000032, "%{RC} f%{D},f%{A},f%{C}" },
	{ "fmsubs",	0xfc00003e, 0xec000038, "%{RC} f%{D},f%{A},f%{C},f%{B}" },
	{ "fmadds",	0xfc00003e, 0xec00003a, "%{RC} f%{D},f%{A},f%{C},f%{B}" },
	{ "fnmsubs",	0xfc00003e, 0xec00003c, "%{RC} f%{D},f%{A},f%{C},f%{B}" },
	{ "fnmadds",	0xfc00003e, 0xec00003e, "%{RC} f%{D},f%{A},f%{C},f%{B}" },
	{ "",		0x0,		0x0, "" }
};

/* 3e * 4 = f8 */
const struct opcode opcodes_3e[] = {
	{ "std",	0xfc000003, 0xf8000000, " %{D},%{ds}(%{A})" },
	{ "stdu",	0xfc000003, 0xf8000001, " %{D},%{ds}(%{A})" },
	{ "",		0x0,		0x0, "" }
};

/* 3f * 4 = fc */
const struct opcode opcodes_3f[] = {
	{ "fcmpu",	0xfc0007fe, 0xfc000000, " %{crfD},f%{A},f%{B}" },
	{ "frsp",	0xfc0007fe, 0xfc000018, "%{RC} f%{D},f%{B}" },
	{ "fctiw",	0xfc0007fe, 0xfc00001c, "%{RC} f%{D},f%{B}" },
	{ "fctiwz",	0xfc0007fe, 0xfc00001e, "%{RC} f%{D},f%{B}" },

	{ "fdiv",	0xfc00003e, 0xfc000024, "%{RC} f%{D},f%{A},f%{B}" },
	{ "fsub",	0xfc00003e, 0xfc000028, "%{RC} f%{D},f%{A},f%{B}" },
	{ "fadd",	0xfc00003e, 0xfc00002a, "%{RC} f%{D},f%{A},f%{B}" },
	{ "fsqrt",	0xfc00003e, 0xfc00002c, "%{RC} f%{D},f%{B}" },
	{ "fsel",	0xfc00003e, 0xfc00002e, "%{RC} f%{D},f%{A},f%{C},f%{B}" },
	{ "fmul",	0xfc00003e, 0xfc000032, "%{RC} f%{D},f%{A},f%{C}" },
	{ "frsqrte",	0xfc00003e, 0xfc000034, "%{RC} f%{D},f%{B}" },
	{ "fmsub",	0xfc00003e, 0xfc000038, "%{RC} f%{D},f%{A},f%{C},f%{B}" },
	{ "fmadd",	0xfc00003e, 0xfc00003a, "%{RC} f%{D},f%{A},f%{C},f%{B}" },
	{ "fnmsub",	0xfc00003e, 0xfc00003c, "%{RC} f%{D},f%{A},f%{C},f%{B}" },
	{ "fnmadd",	0xfc00003e, 0xfc00003e, "%{RC} f%{D},f%{A},f%{C},f%{B}" },

	{ "fcmpo",	0xfc0007fe, 0xfc000040, "%{RC} f%{D},f%{A},f%{C}" },
	{ "mtfsb1",	0xfc0007fe, 0xfc00004c, "%{RC} f%{D},f%{A},f%{C}" },
	{ "fneg",	0xfc0007fe, 0xfc000050, "%{RC} f%{D},f%{A},f%{C}" },
	{ "mcrfs",	0xfc0007fe, 0xfc000080, "%{RC} f%{D},f%{A},f%{C}" },
	{ "mtfsb0",	0xfc0007fe, 0xfc00008c, "%{RC} %{crfD},f%{C}" },
	{ "fmr",	0xfc0007fe, 0xfc000090, "%{RC} f%{D},f%{B}" },
	{ "mtfsfi",	0xfc0007fe, 0xfc00010c, "%{RC} %{crfD},f%{C},%{IMM}" },

	{ "fnabs",	0xfc0007fe, 0xfc000110, "%{RC} f%{D},f%{B}" },
	{ "fabs",	0xfc0007fe, 0xfc000210, "%{RC} f%{D},f%{B}" },
	{ "mffs",	0xfc0007fe, 0xfc00048e, "%{RC} f%{D},f%{B}" },
	{ "mtfsf",	0xfc0007fe, 0xfc00058e, "%{RC} %{FM},f%{B}" },
	{ "fctid",	0xfc0007fe, 0xfc00065c, "%{RC} f%{D},f%{B}" },
	{ "fctidz",	0xfc0007fe, 0xfc00065e, "%{RC} f%{D},f%{B}" },
	{ "fcfid",	0xfc0007fe, 0xfc00069c, "%{RC} f%{D},f%{B}" },
	{ "",		0x0,		0x0, "" }
};

void
op_ill(u_int32_t addr, instr_t instr)
{
	db_printf("illegal instruction %x\n", instr);
}

/*
 * Extracts bits out of an instruction opcode, base indicates the lsb
 * to keep.
 * Note that this uses the PowerPC bit number for base, MSb == 0
 * because all of the documentation is written that way.
 */
u_int32_t
extract_field(u_int32_t value, u_int32_t base, u_int32_t width)
{
	u_int32_t mask = (1 << width) - 1;
	return ((value >> (31 - base)) & mask);
}

char *db_BOBI_cond[] = {
	"ge",
	"le",
	"ne",
	"ns",
	"lt",
	"gt",
	"eq",
	"so"
};
/* what about prediction directions? */
char *db_BO_op[] = {
	"dnzf",
	"dnzf-",
	"dzf",
	"dzf-",
	"",
	"",
	"",
	"",
	"dnzt",
	"dnzt-",
	"dzt",
	"dzt-",
	"",
	"",
	"",
	"",
	"dnz",
	"dnz",
	"dz",
	"dz",
	"",
	"",
	"",
	"",
	"dnz",
	"dnz",
	"dz",
	"dz",
	"",
	"",
	"",
	""
};

char *BItbl[] = {
	"", "gt", "eq", "so"
};

char BO_uses_tbl[32] = {
	/* 0 */ 1,
	/* 1 */ 1,
	/* 2 */ 1,
	/* 3 */ 1,
	/* 4 */ 0,
	/* 5 */ 0,
	/* 6 */ 0, /* invalid */
	/* 7 */ 0, /* invalid */
	/* 8 */ 1,
	/* 9 */ 1,
	/* a */ 1,
	/* b */ 1,
	/* c */ 0,
	/* d */ 0,
	/* e */ 0, /* invalid */
	/* f */ 1,
	/* 10 */        1,
	/* 11 */        1,
	/* 12 */        1,
	/* 13 */        1,
	/* 14 */        1,
	/* 15 */        0, /* invalid */
	/* 16 */        0, /* invalid */
	/* 17 */        0, /* invalid */
	/* 18 */        0, /* invalid */
	/* 19 */        0, /* invalid */
	/* 1a */        0, /* invalid */
	/* 1b */        0, /* invalid */
	/* 1c */        0, /* invalid */
	/* 1d */        0, /* invalid */
	/* 1e */        0, /* invalid */
	/* 1f */        0, /* invalid */
};

void
disasm_process_field(u_int32_t addr, instr_t instr, char **ppfmt,
    char *disasm_buf, size_t bufsize)
{
	char field [8];
	char lbuf[50];
	int i;
	char *pfmt = *ppfmt;
	enum opf opf;
	const char *name;
	db_expr_t offset;

	/* find field */
	if (pfmt[0] != '%' || pfmt[1] != '{') {
		printf("error in disasm fmt [%s]\n", pfmt);
	}
	pfmt = &pfmt[2];
	for (i = 0;
	    pfmt[i] != '\0' && pfmt[i] != '}' && i < sizeof(field);
	    i++) {
		field[i] = pfmt[i];
	}
	if (i == sizeof(field)) {
		printf("error in disasm fmt [%s]\n", pfmt);
		return;
	}
	field[i] = 0;
	if (pfmt[i] == '\0') {
		/* match following close paren { */
		printf("disasm_process_field: missing } in [%s]\n", pfmt);
	}
	*ppfmt = &pfmt[i+1];
	opf = Opf_INVALID;
	for (i = 0; db_fields[i].name != NULL; i++) {
		if (strcmp(db_fields[i].name, field) == 0) {
			opf = db_fields[i].opf;
			break;
		}
	}
	switch (opf) {
	case Opf_INVALID:
		{
			printf("unable to find variable [%s]\n", field);
		}
	case Opf_A:
		{
			u_int A;
			A = extract_field(instr, 15, 5);
			snprintf(lbuf, sizeof (lbuf), "r%d", A);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_A0:
		{
			u_int A;
			A = extract_field(instr, 15, 5);
			if (A != 0) {
				snprintf(lbuf, sizeof (lbuf), "r%d,", A);
				strlcat (disasm_buf, lbuf, bufsize);
			}
		}
		break;
	case Opf_AA:
		if (instr & 0x2) {
			strlcat (disasm_buf, "a", bufsize);
		}
		break;
	case Opf_LI:
		{
			u_int LI;
			LI = extract_field(instr, 29, 24);
			LI = LI << 2;
			if (LI & 0x02000000) {
				LI |= ~0x03ffffff;
			}
			if ((instr & (1 << 1)) == 0) {
				/* CHECK AA bit */
				LI = addr + LI;
			}
			db_find_sym_and_offset(LI, &name, &offset);
			if (name) {
				if (offset == 0) {
					snprintf(lbuf, sizeof (lbuf),
					    "0x%x (%s)", LI, name);
					strlcat (disasm_buf, lbuf, bufsize);
				} else {
					snprintf(lbuf, sizeof (lbuf),
					    "0x%x (%s+0x%lx)", LI, name,
					    offset);
					strlcat (disasm_buf, lbuf, bufsize);
				}
			} else {
				snprintf(lbuf, sizeof (lbuf), "0x%x", LI);
				strlcat (disasm_buf, lbuf, bufsize);
			}
		}
		break;
	case Opf_B:
		{
			u_int B;
			B = extract_field(instr, 20, 5);
			snprintf(lbuf, sizeof (lbuf), "r%d", B);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_BD:
		{
			int BD;
			BD = extract_field(instr, 29, 14);
			BD = BD << 2;
			if (BD & 0x00008000) {
				BD |= ~0x00007fff;
			}
			if ((instr & (1 << 1)) == 0) {
				/* CHECK AA bit */
				BD = addr + BD;
			}
			db_find_sym_and_offset(BD, &name, &offset);
			if (name) {
				if (offset == 0) {
					snprintf(lbuf, sizeof (lbuf),
					    "0x%x (%s)", BD, name);
					strlcat (disasm_buf, lbuf, bufsize);
				} else {
					snprintf(lbuf, sizeof (lbuf),
					    "0x%x (%s+0x%lx)", BD, name, offset);
					strlcat (disasm_buf, lbuf, bufsize);
				}
			} else {
				snprintf(lbuf, sizeof (lbuf), "0x%x", BD);
				strlcat (disasm_buf, lbuf, bufsize);
			}
		}
		break;
	case Opf_BI1:
	case Opf_BI:
		{
			int BO, BI, cr, printcomma = 0;
			BO = extract_field(instr, 10, 5);
			BI = extract_field(instr, 15, 5);
			cr =  (BI >> 2) & 7;
			if (cr != 0) {
				snprintf(lbuf, sizeof (lbuf), "cr%d", cr);
				strlcat (disasm_buf, lbuf, bufsize);
				printcomma = 1;
			}
			if (BO_uses_tbl[BO]) {
				if ((cr != 0) && ((BI & 3) != 0) &&
				    BO_uses_tbl[BO] != 0)
					strlcat (disasm_buf, "+", bufsize);

				snprintf(lbuf, sizeof (lbuf), "%s",
				    BItbl[BI & 3]);
				strlcat (disasm_buf, lbuf, bufsize);
				printcomma = 1;
			}
			if ((opf == Opf_BI) && printcomma)
				strlcat (disasm_buf, ",", bufsize);
		}
		break;
	case Opf_BO:
		{
			int BO, BI;
			BO = extract_field(instr, 10, 5);
			strlcat (disasm_buf, db_BO_op[BO], bufsize);
			if ((BO & 4) != 0) {
				BI = extract_field(instr, 15, 5);
				strlcat (disasm_buf,
				    db_BOBI_cond[(BI & 0x3)| (((BO & 8) >> 1))],
				    bufsize);

				if (BO & 1)
					strlcat (disasm_buf, "-", bufsize);
			}
		}
		break;
	case Opf_C:
		{
			u_int C;
			C = extract_field(instr, 25, 5);
			snprintf(lbuf, sizeof (lbuf), "r%d, ", C);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_CRM:
		{
			u_int CRM;
			CRM = extract_field(instr, 19, 8);
			snprintf(lbuf, sizeof (lbuf), "0x%x", CRM);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_FM:
		{
			u_int FM;
			FM = extract_field(instr, 10, 8);
			snprintf(lbuf, sizeof (lbuf), "%d", FM);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_LK:
		if (instr & 0x1) {
			strlcat (disasm_buf, "l", bufsize);
		}
		break;
	case Opf_MB:
		{
			u_int MB;
			MB = extract_field(instr, 20, 5);
			snprintf(lbuf, sizeof (lbuf), "%d", MB);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_ME:
		{
			u_int ME;
			ME = extract_field(instr, 25, 5);
			snprintf(lbuf, sizeof (lbuf), "%d", ME);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_NB:
		{
			u_int NB;
			NB = extract_field(instr, 20, 5);
			if (NB == 0 ) {
				NB=32;
			}
			snprintf(lbuf, sizeof (lbuf), "%d", NB);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_OE:
		if (instr & (1 << (31-21))) {
			strlcat (disasm_buf, "o", bufsize);
		}
		break;
	case Opf_RC:
		if (instr & 0x1) {
			strlcat (disasm_buf, ".", bufsize);
		}
		break;
	case Opf_S:
	case Opf_D:
		{
			u_int D;
			/* S and D are the same */
			D = extract_field(instr, 10, 5);
			snprintf(lbuf, sizeof (lbuf), "r%d", D);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_SH:
		{
			u_int SH;
			SH = extract_field(instr, 20, 5);
			snprintf(lbuf, sizeof (lbuf), "%d", SH);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_SIMM:
	case Opf_d:
		{
			int IMM;
			IMM = extract_field(instr, 31, 16);
			if (IMM & 0x8000)
				IMM |= ~0x7fff;
			snprintf(lbuf, sizeof (lbuf), "%d", IMM);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_UIMM:
		{
			u_int IMM;
			IMM = extract_field(instr, 31, 16);
			snprintf(lbuf, sizeof (lbuf), "0x%x", IMM);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_SR:
		{
			u_int SR;
			SR = extract_field(instr, 15, 3);
			snprintf(lbuf, sizeof (lbuf), "sr%d", SR);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_TO:
		{
			u_int TO;
			TO = extract_field(instr, 10, 1);
			snprintf(lbuf, sizeof (lbuf), "%d", TO);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_crbA:
		{
			u_int crbA;
			crbA = extract_field(instr, 15, 5);
			snprintf(lbuf, sizeof (lbuf), "%d", crbA);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_crbB:
		{
			u_int crbB;
			crbB = extract_field(instr, 20, 5);
			snprintf(lbuf, sizeof (lbuf), "%d", crbB);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_crbD:
		{
			u_int crfD;
			crfD = extract_field(instr, 8, 3);
			snprintf(lbuf, sizeof (lbuf), "crf%d", crfD);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_crfD:
		{
			u_int crfD;
			crfD = extract_field(instr, 8, 3);
			snprintf(lbuf, sizeof (lbuf), "crf%d", crfD);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_crfS:
		{
			u_int crfS;
			crfS = extract_field(instr, 13, 3);
			snprintf(lbuf, sizeof (lbuf), "%d", crfS);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_ds:
		{
			int ds;
			ds = extract_field(instr, 29, 14);
			ds = ds << 2;
			if (ds & 0x8000)
				ds |= ~0x7fff;
			snprintf(lbuf, sizeof (lbuf), "%d", ds);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_mb:
		{
			u_int mb, mbl, mbh;
			mbl = extract_field(instr, 25, 4);
			mbh = extract_field(instr, 26, 1);
			mb = mbh << 4 | mbl;
			snprintf(lbuf, sizeof (lbuf), ", %d", mb);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_sh:
		{
			u_int sh, shl, shh;
			shl = extract_field(instr, 19, 4);
			shh = extract_field(instr, 20, 1);
			sh = shh << 4 | shl;
			snprintf(lbuf, sizeof (lbuf), ", %d", sh);
			strlcat (disasm_buf, lbuf, bufsize);
		}
		break;
	case Opf_spr:
		{
			u_int spr;
			u_int sprl;
			u_int sprh;
			char *reg;
			sprl = extract_field(instr, 15, 5);
			sprh = extract_field(instr, 20, 5);
			spr = sprh << 5 | sprl;

			/* this table could be written better */
			switch (spr) {
			case	1:
				reg = "xer";
				break;
			case	8:
				reg = "lr";
				break;
			case	9:
				reg = "ctr";
				break;
			case	18:
				reg = "dsisr";
				break;
			case	19:
				reg = "dar";
				break;
			case	22:
				reg = "dec";
				break;
			case	25:
				reg = "sdr1";
				break;
			case	26:
				reg = "srr0";
				break;
			case	27:
				reg = "srr1";
				break;
			case	272:
				reg = "SPRG0";
				break;
			case	273:
				reg = "SPRG1";
				break;
			case	274:
				reg = "SPRG3";
				break;
			case	275:
				reg = "SPRG3";
				break;
			case	280:
				reg = "asr";
				break;
			case	282:
				reg = "aer";
				break;
			case	287:
				reg = "pvr";
				break;
			case	528:
				reg = "ibat0u";
				break;
			case	529:
				reg = "ibat0l";
				break;
			case	530:
				reg = "ibat1u";
				break;
			case	531:
				reg = "ibat1l";
				break;
			case	532:
				reg = "ibat2u";
				break;
			case	533:
				reg = "ibat2l";
				break;
			case	534:
				reg = "ibat3u";
				break;
			case	535:
				reg = "ibat3l";
				break;
			case	536:
				reg = "dbat0u";
				break;
			case	537:
				reg = "dbat0l";
				break;
			case	538:
				reg = "dbat1u";
				break;
			case	539:
				reg = "dbat1l";
				break;
			case	540:
				reg = "dbat2u";
				break;
			case	541:
				reg = "dbat2l";
				break;
			case	542:
				reg = "dbat3u";
				break;
			case	543:
				reg = "dbat3l";
				break;
			case	1013:
				reg = "dabr";
				break;
			default:
				reg = 0;
			}
			if (reg == 0) {
				snprintf(lbuf, sizeof (lbuf), "spr%d", spr);
				strlcat (disasm_buf, lbuf, bufsize);
			} else {
				snprintf(lbuf, sizeof (lbuf), "%s", reg);
				strlcat (disasm_buf, lbuf, bufsize);
			}
		}
		break;
	case Opf_tbr:
		{
			u_int tbr;
			u_int tbrl;
			u_int tbrh;
			char *reg = NULL;
			tbrl = extract_field(instr, 15, 5);
			tbrh = extract_field(instr, 20, 5);
			tbr = tbrh << 5 | tbrl;

			switch (tbr) {
			case 268:
				reg = "tbl";
				break;
			case 269:
				reg = "tbu";
				break;
			default:
				reg = 0;
			}
			if (reg == NULL) {
				snprintf(lbuf, sizeof (lbuf), "tbr%d", tbr);
				strlcat (disasm_buf, lbuf, bufsize);
			} else {
				snprintf(lbuf, sizeof (lbuf), "%s", reg);
				strlcat (disasm_buf, lbuf, bufsize);
			}
		}
		break;
	}
}

void
disasm_fields(u_int32_t addr, const struct opcode *popcode, instr_t instr,
    char *disasm_str, size_t bufsize)
{
	char *pfmt;
	char cbuf[2];
	if (popcode->decode_str == NULL || popcode->decode_str[0] == '0') {
		return;
	}
	pfmt = popcode->decode_str;
	disasm_str[0] = '\0';

	while (*pfmt != '\0')  {
		if (*pfmt == '%') {
			disasm_process_field(addr, instr, &pfmt, disasm_str,
			    bufsize);
		} else {
			cbuf[0] = *pfmt;
			cbuf[1] = '\0';
			strlcat(disasm_str, cbuf, bufsize);
			pfmt++;
		}
	}
}

void
op_base(u_int32_t addr, instr_t instr)
{
	dis_ppc(addr, opcodes, instr);
}

void
op_cl_x13(u_int32_t addr, instr_t instr)
{
	dis_ppc(addr, opcodes_13, instr);
}

void
op_cl_x1e(u_int32_t addr, instr_t instr)
{
	dis_ppc(addr, opcodes_1e, instr);
}

void
op_cl_x1f(u_int32_t addr, instr_t instr)
{
	dis_ppc(addr, opcodes_1f, instr);
}

void
op_cl_x3a(u_int32_t addr, instr_t instr)
{
	dis_ppc(addr, opcodes_3a, instr);
}

void
op_cl_x3b(u_int32_t addr, instr_t instr)
{
	dis_ppc(addr, opcodes_3b, instr);
}

void
op_cl_x3e(u_int32_t addr, instr_t instr)
{
	dis_ppc(addr, opcodes_3e, instr);
}

void
op_cl_x3f(u_int32_t addr, instr_t instr)
{
	dis_ppc(addr, opcodes_3f, instr);
}

void
dis_ppc(u_int32_t addr, const struct opcode *opcodeset, instr_t instr)
{
	const struct opcode *op;
	int i;
	char disasm_str[80];

	for (i=0; opcodeset[i].mask != 0; i++) {
		op = &opcodeset[i];
		if ((instr & op->mask) == op->code) {
			disasm_fields(addr, op, instr, disasm_str,
			    sizeof disasm_str);
			db_printf("%s%s\n", op->name, disasm_str);
			return;
		}
	}
	op_ill(addr, instr);
}

vaddr_t
db_disasm(vaddr_t loc, int extended)
{
	int class;
	instr_t opcode;
	opcode = *(instr_t *)(loc);
	class = opcode >> 26;
	(opcodes_base[class])(loc, opcode);

	return loc + 4;
}
