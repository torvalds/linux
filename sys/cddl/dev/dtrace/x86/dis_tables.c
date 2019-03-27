/*
 *
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2016 Joyent, Inc.
 */

/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * $FreeBSD$
 */

#include	"dis_tables.h"

/* BEGIN CSTYLED */

/*
 * Disassembly begins in dis_distable, which is equivalent to the One-byte
 * Opcode Map in the Intel IA32 ISA Reference (page A-6 in my copy).  The
 * decoding loops then traverse out through the other tables as necessary to
 * decode a given instruction.
 *
 * The behavior of this file can be controlled by one of the following flags:
 *
 * 	DIS_TEXT	Include text for disassembly
 * 	DIS_MEM		Include memory-size calculations
 *
 * Either or both of these can be defined.
 *
 * This file is not, and will never be, cstyled.  If anything, the tables should
 * be taken out another tab stop or two so nothing overlaps.
 */

/*
 * These functions must be provided for the consumer to do disassembly.
 */
#ifdef DIS_TEXT
extern char *strncpy(char *, const char *, size_t);
extern size_t strlen(const char *);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);
extern size_t strlcat(char *, const char *, size_t);
#endif


#define		TERM 	0	/* used to indicate that the 'indirect' */
				/* field terminates - no pointer.	*/

/* Used to decode instructions. */
typedef struct	instable {
	struct instable	*it_indirect;	/* for decode op codes */
	uchar_t		it_adrmode;
#ifdef DIS_TEXT
	char		it_name[NCPS];
	uint_t		it_suffix:1;		/* mnem + "w", "l", or "d" */
#endif
#ifdef DIS_MEM
	uint_t		it_size:16;
#endif
	uint_t		it_invalid64:1;		/* opcode invalid in amd64 */
	uint_t		it_always64:1;		/* 64 bit when in 64 bit mode */
	uint_t		it_invalid32:1;		/* invalid in IA32 */
	uint_t		it_stackop:1;		/* push/pop stack operation */
	uint_t		it_vexwoxmm:1;		/* VEX instructions that don't use XMM/YMM */
	uint_t		it_avxsuf:1;		/* AVX suffix required */
} instable_t;

/*
 * Instruction formats.
 */
enum {
	UNKNOWN,
	MRw,
	IMlw,
	IMw,
	IR,
	OA,
	AO,
	MS,
	SM,
	Mv,
	Mw,
	M,		/* register or memory */
	MG9,		/* register or memory in group 9 (prefix optional) */
	Mb,		/* register or memory, always byte sized */
	MO,		/* memory only (no registers) */
	PREF,
	SWAPGS_RDTSCP,
	MONITOR_MWAIT,
	R,
	RA,
	SEG,
	MR,
	RM,
	RM_66r,		/* RM, but with a required 0x66 prefix */ 
	IA,
	MA,
	SD,
	AD,
	SA,
	D,
	INM,
	SO,
	BD,
	I,
	P,
	V,
	DSHIFT,		/* for double shift that has an 8-bit immediate */
	U,
	OVERRIDE,
	NORM,		/* instructions w/o ModR/M byte, no memory access */
	IMPLMEM,	/* instructions w/o ModR/M byte, implicit mem access */
	O,		/* for call	*/
	JTAB,		/* jump table 	*/
	IMUL,		/* for 186 iimul instr  */
	CBW,		/* so data16 can be evaluated for cbw and variants */
	MvI,		/* for 186 logicals */
	ENTER,		/* for 186 enter instr  */
	RMw,		/* for 286 arpl instr */
	Ib,		/* for push immediate byte */
	F,		/* for 287 instructions */
	FF,		/* for 287 instructions */
	FFC,		/* for 287 instructions */
	DM,		/* 16-bit data */
	AM,		/* 16-bit addr */
	LSEG,		/* for 3-bit seg reg encoding */
	MIb,		/* for 386 logicals */
	SREG,		/* for 386 special registers */
	PREFIX,		/* a REP instruction prefix */
	LOCK,		/* a LOCK instruction prefix */
	INT3,		/* The int 3 instruction, which has a fake operand */
	INTx,		/* The normal int instruction, with explicit int num */
	DSHIFTcl,	/* for double shift that implicitly uses %cl */
	CWD,		/* so data16 can be evaluated for cwd and variants */
	RET,		/* single immediate 16-bit operand */
	MOVZ,		/* for movs and movz, with different size operands */
	CRC32,		/* for crc32, with different size operands */
	XADDB,		/* for xaddb */
	MOVSXZ,		/* AMD64 mov sign extend 32 to 64 bit instruction */
	MOVBE,		/* movbe instruction */

/*
 * MMX/SIMD addressing modes.
 */

	MMO,		/* Prefixable MMX/SIMD-Int	mm/mem	-> mm */
	MMOIMPL,	/* Prefixable MMX/SIMD-Int	mm	-> mm (mem) */
	MMO3P,		/* Prefixable MMX/SIMD-Int	mm	-> r32,imm8 */
	MMOM3,		/* Prefixable MMX/SIMD-Int	mm	-> r32 	*/
	MMOS,		/* Prefixable MMX/SIMD-Int	mm	-> mm/mem */
	MMOMS,		/* Prefixable MMX/SIMD-Int	mm	-> mem */
	MMOPM,		/* MMX/SIMD-Int			mm/mem	-> mm,imm8 */
	MMOPM_66o,	/* MMX/SIMD-Int 0x66 optional	mm/mem	-> mm,imm8 */
	MMOPRM,		/* Prefixable MMX/SIMD-Int	r32/mem	-> mm,imm8 */
	MMOSH,		/* Prefixable MMX		mm,imm8	*/
	MM,		/* MMX/SIMD-Int			mm/mem	-> mm	*/
	MMS,		/* MMX/SIMD-Int			mm	-> mm/mem */
	MMSH,		/* MMX				mm,imm8 */
	XMMO,		/* Prefixable SIMD		xmm/mem	-> xmm */
	XMMOS,		/* Prefixable SIMD		xmm	-> xmm/mem */
	XMMOPM,		/* Prefixable SIMD		xmm/mem	w/to xmm,imm8 */
	XMMOMX,		/* Prefixable SIMD		mm/mem	-> xmm */
	XMMOX3,		/* Prefixable SIMD		xmm	-> r32 */
	XMMOXMM,	/* Prefixable SIMD		xmm/mem	-> mm	*/
	XMMOM,		/* Prefixable SIMD		xmm	-> mem */
	XMMOMS,		/* Prefixable SIMD		mem	-> xmm */
	XMM,		/* SIMD 			xmm/mem	-> xmm */
	XMM_66r,	/* SIMD 0x66 prefix required	xmm/mem	-> xmm */
	XMM_66o,	/* SIMD 0x66 prefix optional 	xmm/mem	-> xmm */
	XMMXIMPL,	/* SIMD				xmm	-> xmm (mem) */
	XMM3P,		/* SIMD				xmm	-> r32,imm8 */
	XMM3PM_66r,	/* SIMD 0x66 prefix required	xmm	-> r32/mem,imm8 */
	XMMP,		/* SIMD 			xmm/mem w/to xmm,imm8 */
	XMMP_66o,	/* SIMD 0x66 prefix optional	xmm/mem w/to xmm,imm8 */
	XMMP_66r,	/* SIMD 0x66 prefix required	xmm/mem w/to xmm,imm8 */
	XMMPRM,		/* SIMD 			r32/mem -> xmm,imm8 */
	XMMPRM_66r,	/* SIMD 0x66 prefix required	r32/mem -> xmm,imm8 */
	XMMS,		/* SIMD				xmm	-> xmm/mem */
	XMMM,		/* SIMD 			mem	-> xmm */
	XMMM_66r,	/* SIMD	0x66 prefix required	mem	-> xmm */
	XMMMS,		/* SIMD				xmm	-> mem */
	XMM3MX,		/* SIMD 			r32/mem -> xmm */
	XMM3MXS,	/* SIMD 			xmm	-> r32/mem */
	XMMSH,		/* SIMD 			xmm,imm8 */
	XMMXM3,		/* SIMD 			xmm/mem -> r32 */
	XMMX3,		/* SIMD 			xmm	-> r32 */
	XMMXMM,		/* SIMD 			xmm/mem	-> mm */
	XMMMX,		/* SIMD 			mm	-> xmm */
	XMMXM,		/* SIMD 			xmm	-> mm */
        XMMX2I,		/* SIMD				xmm -> xmm, imm, imm */
        XMM2I,		/* SIMD				xmm, imm, imm */
	XMMFENCE,	/* SIMD lfence or mfence */
	XMMSFNC,	/* SIMD sfence (none or mem) */
	XGETBV_XSETBV,
	VEX_NONE,	/* VEX  no operand */
	VEX_MO,		/* VEX	mod_rm		               -> implicit reg */
	VEX_RMrX,	/* VEX  VEX.vvvv, mod_rm               -> mod_reg */
	VEX_VRMrX,	/* VEX  mod_rm, VEX.vvvv               -> mod_rm */
	VEX_RRX,	/* VEX  VEX.vvvv, mod_reg              -> mod_rm */
	VEX_RMRX,	/* VEX  VEX.vvvv, mod_rm, imm8[7:4]    -> mod_reg */
	VEX_MX,         /* VEX  mod_rm                         -> mod_reg */
	VEX_MXI,        /* VEX  mod_rm, imm8                   -> mod_reg */
	VEX_XXI,        /* VEX  mod_rm, imm8                   -> VEX.vvvv */
	VEX_MR,         /* VEX  mod_rm                         -> mod_reg */
	VEX_RRI,        /* VEX  mod_reg, mod_rm                -> implicit(eflags/r32) */
	VEX_RX,         /* VEX  mod_reg                        -> mod_rm */
	VEX_RR,         /* VEX  mod_rm                         -> mod_reg */
	VEX_RRi,        /* VEX  mod_rm, imm8                   -> mod_reg */
	VEX_RM,         /* VEX  mod_reg                        -> mod_rm */
	VEX_RIM,	/* VEX  mod_reg, imm8                  -> mod_rm */
	VEX_RRM,        /* VEX  VEX.vvvv, mod_reg              -> mod_rm */
	VEX_RMX,        /* VEX  VEX.vvvv, mod_rm               -> mod_reg */
	VEX_SbVM,	/* VEX  SIB, VEX.vvvv                  -> mod_rm */
	VMx,		/* vmcall/vmlaunch/vmresume/vmxoff */
	VMxo,		/* VMx instruction with optional prefix */
	SVM,		/* AMD SVM instructions */
	BLS,		/* BLSR, BLSMSK, BLSI */
	FMA,		/* FMA instructions, all VEX_RMrX */
	ADX		/* ADX instructions, support REX.w, mod_rm->mod_reg */
};

/*
 * VEX prefixes
 */
#define VEX_2bytes	0xC5	/* the first byte of two-byte form */
#define VEX_3bytes	0xC4	/* the first byte of three-byte form */

#define	FILL	0x90	/* Fill byte used for alignment (nop)	*/

/*
** Register numbers for the i386
*/
#define	EAX_REGNO 0
#define	ECX_REGNO 1
#define	EDX_REGNO 2
#define	EBX_REGNO 3
#define	ESP_REGNO 4
#define	EBP_REGNO 5
#define	ESI_REGNO 6
#define	EDI_REGNO 7

/*
 * modes for immediate values
 */
#define	MODE_NONE	0
#define	MODE_IPREL	1	/* signed IP relative value */
#define	MODE_SIGNED	2	/* sign extended immediate */
#define	MODE_IMPLIED	3	/* constant value implied from opcode */
#define	MODE_OFFSET	4	/* offset part of an address */
#define	MODE_RIPREL	5	/* like IPREL, but from %rip (amd64) */

/*
 * The letters used in these macros are:
 *   IND - indirect to another to another table
 *   "T" - means to Terminate indirections (this is the final opcode)
 *   "S" - means "operand length suffix required"
 *   "Sa" - means AVX2 suffix (d/q) required
 *   "NS" - means "no suffix" which is the operand length suffix of the opcode
 *   "Z" - means instruction size arg required
 *   "u" - means the opcode is invalid in IA32 but valid in amd64
 *   "x" - means the opcode is invalid in amd64, but not IA32
 *   "y" - means the operand size is always 64 bits in 64 bit mode
 *   "p" - means push/pop stack operation
 *   "vr" - means VEX instruction that operates on normal registers, not fpu
 */

#if defined(DIS_TEXT) && defined(DIS_MEM)
#define	IND(table)		{(instable_t *)table, 0, "", 0, 0, 0, 0, 0, 0}
#define	INDx(table)		{(instable_t *)table, 0, "", 0, 0, 1, 0, 0, 0}
#define	TNS(name, amode)	{TERM, amode, name, 0, 0, 0, 0, 0, 0}
#define	TNSu(name, amode)	{TERM, amode, name, 0, 0, 0, 0, 1, 0}
#define	TNSx(name, amode)	{TERM, amode, name, 0, 0, 1, 0, 0, 0}
#define	TNSy(name, amode)	{TERM, amode, name, 0, 0, 0, 1, 0, 0}
#define	TNSyp(name, amode)	{TERM, amode, name, 0, 0, 0, 1, 0, 1}
#define	TNSZ(name, amode, sz)	{TERM, amode, name, 0, sz, 0, 0, 0, 0}
#define	TNSZy(name, amode, sz)	{TERM, amode, name, 0, sz, 0, 1, 0, 0}
#define	TNSZvr(name, amode, sz)	{TERM, amode, name, 0, sz, 0, 0, 0, 0, 1}
#define	TS(name, amode)		{TERM, amode, name, 1, 0, 0, 0, 0, 0}
#define	TSx(name, amode)	{TERM, amode, name, 1, 0, 1, 0, 0, 0}
#define	TSy(name, amode)	{TERM, amode, name, 1, 0, 0, 1, 0, 0}
#define	TSp(name, amode)	{TERM, amode, name, 1, 0, 0, 0, 0, 1}
#define	TSZ(name, amode, sz)	{TERM, amode, name, 1, sz, 0, 0, 0, 0}
#define	TSaZ(name, amode, sz)	{TERM, amode, name, 1, sz, 0, 0, 0, 0, 0, 1}
#define	TSZx(name, amode, sz)	{TERM, amode, name, 1, sz, 1, 0, 0, 0}
#define	TSZy(name, amode, sz)	{TERM, amode, name, 1, sz, 0, 1, 0, 0}
#define	INVALID			{TERM, UNKNOWN, "", 0, 0, 0, 0, 0}
#elif defined(DIS_TEXT)
#define	IND(table)		{(instable_t *)table, 0, "", 0, 0, 0, 0, 0}
#define	INDx(table)		{(instable_t *)table, 0, "", 0, 1, 0, 0, 0}
#define	TNS(name, amode)	{TERM, amode, name, 0, 0, 0, 0, 0}
#define	TNSu(name, amode)	{TERM, amode, name, 0, 0, 0, 1, 0}
#define	TNSx(name, amode)	{TERM, amode, name, 0, 1, 0, 0, 0}
#define	TNSy(name, amode)	{TERM, amode, name, 0, 0, 1, 0, 0}
#define	TNSyp(name, amode)	{TERM, amode, name, 0, 0, 1, 0, 1}
#define	TNSZ(name, amode, sz)	{TERM, amode, name, 0, 0, 0, 0, 0}
#define	TNSZy(name, amode, sz)	{TERM, amode, name, 0, 0, 1, 0, 0}
#define	TNSZvr(name, amode, sz)	{TERM, amode, name, 0, 0, 0, 0, 0, 1}
#define	TS(name, amode)		{TERM, amode, name, 1, 0, 0, 0, 0}
#define	TSx(name, amode)	{TERM, amode, name, 1, 1, 0, 0, 0}
#define	TSy(name, amode)	{TERM, amode, name, 1, 0, 1, 0, 0}
#define	TSp(name, amode)	{TERM, amode, name, 1, 0, 0, 0, 1}
#define	TSZ(name, amode, sz)	{TERM, amode, name, 1, 0, 0, 0, 0}
#define	TSaZ(name, amode, sz)	{TERM, amode, name, 1, 0, 0, 0, 0, 0, 1}
#define	TSZx(name, amode, sz)	{TERM, amode, name, 1, 1, 0, 0, 0}
#define	TSZy(name, amode, sz)	{TERM, amode, name, 1, 0, 1, 0, 0}
#define	INVALID			{TERM, UNKNOWN, "", 0, 0, 0, 0, 0}
#elif defined(DIS_MEM)
#define	IND(table)		{(instable_t *)table, 0, 0, 0, 0, 0, 0}
#define	INDx(table)		{(instable_t *)table, 0, 0, 1, 0, 0, 0}
#define	TNS(name, amode)	{TERM, amode,  0, 0, 0, 0, 0}
#define	TNSu(name, amode)	{TERM, amode,  0, 0, 0, 1, 0}
#define	TNSy(name, amode)	{TERM, amode,  0, 0, 1, 0, 0}
#define	TNSyp(name, amode)	{TERM, amode,  0, 0, 1, 0, 1}
#define	TNSx(name, amode)	{TERM, amode,  0, 1, 0, 0, 0}
#define	TNSZ(name, amode, sz)	{TERM, amode, sz, 0, 0, 0, 0}
#define	TNSZy(name, amode, sz)	{TERM, amode, sz, 0, 1, 0, 0}
#define	TNSZvr(name, amode, sz)	{TERM, amode, sz, 0, 0, 0, 0, 1}
#define	TS(name, amode)		{TERM, amode,  0, 0, 0, 0, 0}
#define	TSx(name, amode)	{TERM, amode,  0, 1, 0, 0, 0}
#define	TSy(name, amode)	{TERM, amode,  0, 0, 1, 0, 0}
#define	TSp(name, amode)	{TERM, amode,  0, 0, 0, 0, 1}
#define	TSZ(name, amode, sz)	{TERM, amode, sz, 0, 0, 0, 0}
#define	TSaZ(name, amode, sz)	{TERM, amode, sz, 0, 0, 0, 0, 0, 1}
#define	TSZx(name, amode, sz)	{TERM, amode, sz, 1, 0, 0, 0}
#define	TSZy(name, amode, sz)	{TERM, amode, sz, 0, 1, 0, 0}
#define	INVALID			{TERM, UNKNOWN, 0, 0, 0, 0, 0}
#else
#define	IND(table)		{(instable_t *)table, 0, 0, 0, 0, 0}
#define	INDx(table)		{(instable_t *)table, 0, 1, 0, 0, 0}
#define	TNS(name, amode)	{TERM, amode,  0, 0, 0, 0}
#define	TNSu(name, amode)	{TERM, amode,  0, 0, 1, 0}
#define	TNSy(name, amode)	{TERM, amode,  0, 1, 0, 0}
#define	TNSyp(name, amode)	{TERM, amode,  0, 1, 0, 1}
#define	TNSx(name, amode)	{TERM, amode,  1, 0, 0, 0}
#define	TNSZ(name, amode, sz)	{TERM, amode,  0, 0, 0, 0}
#define	TNSZy(name, amode, sz)	{TERM, amode,  0, 1, 0, 0}
#define	TNSZvr(name, amode, sz)	{TERM, amode,  0, 0, 0, 0, 1}
#define	TS(name, amode)		{TERM, amode,  0, 0, 0, 0}
#define	TSx(name, amode)	{TERM, amode,  1, 0, 0, 0}
#define	TSy(name, amode)	{TERM, amode,  0, 1, 0, 0}
#define	TSp(name, amode)	{TERM, amode,  0, 0, 0, 1}
#define	TSZ(name, amode, sz)	{TERM, amode,  0, 0, 0, 0}
#define	TSaZ(name, amode, sz)	{TERM, amode,  0, 0, 0, 0, 0, 1}
#define	TSZx(name, amode, sz)	{TERM, amode,  1, 0, 0, 0}
#define	TSZy(name, amode, sz)	{TERM, amode,  0, 1, 0, 0}
#define	INVALID			{TERM, UNKNOWN, 0, 0, 0, 0}
#endif

#ifdef DIS_TEXT
/*
 * this decodes the r_m field for mode's 0, 1, 2 in 16 bit mode
 */
const char *const dis_addr16[3][8] = {
"(%bx,%si)", "(%bx,%di)", "(%bp,%si)", "(%bp,%di)", "(%si)", "(%di)", "",
									"(%bx)",
"(%bx,%si)", "(%bx,%di)", "(%bp,%si)", "(%bp,%di)", "(%si)", "(%di", "(%bp)",
									"(%bx)",
"(%bx,%si)", "(%bx,%di)", "(%bp,%si)", "(%bp,%di)", "(%si)", "(%di)", "(%bp)",
									"(%bx)",
};


/*
 * This decodes 32 bit addressing mode r_m field for modes 0, 1, 2
 */
const char *const dis_addr32_mode0[16] = {
  "(%eax)", "(%ecx)", "(%edx)",  "(%ebx)",  "", "",        "(%esi)",  "(%edi)",
  "(%r8d)", "(%r9d)", "(%r10d)", "(%r11d)", "", "",        "(%r14d)", "(%r15d)"
};

const char *const dis_addr32_mode12[16] = {
  "(%eax)", "(%ecx)", "(%edx)",  "(%ebx)",  "", "(%ebp)",  "(%esi)",  "(%edi)",
  "(%r8d)", "(%r9d)", "(%r10d)", "(%r11d)", "", "(%r13d)", "(%r14d)", "(%r15d)"
};

/*
 * This decodes 64 bit addressing mode r_m field for modes 0, 1, 2
 */
const char *const dis_addr64_mode0[16] = {
 "(%rax)", "(%rcx)", "(%rdx)", "(%rbx)", "",       "(%rip)", "(%rsi)", "(%rdi)",
 "(%r8)",  "(%r9)",  "(%r10)", "(%r11)", "(%r12)", "(%rip)", "(%r14)", "(%r15)"
};
const char *const dis_addr64_mode12[16] = {
 "(%rax)", "(%rcx)", "(%rdx)", "(%rbx)", "",       "(%rbp)", "(%rsi)", "(%rdi)",
 "(%r8)",  "(%r9)",  "(%r10)", "(%r11)", "(%r12)", "(%r13)", "(%r14)", "(%r15)"
};

/*
 * decode for scale from SIB byte
 */
const char *const dis_scale_factor[4] = { ")", ",2)", ",4)", ",8)" };

/*
 * decode for scale from VSIB byte, note that we always include the scale factor
 * to match gas.
 */
const char *const dis_vscale_factor[4] = { ",1)", ",2)", ",4)", ",8)" };

/*
 * register decoding for normal references to registers (ie. not addressing)
 */
const char *const dis_REG8[16] = {
	"%al",  "%cl",  "%dl",   "%bl",   "%ah",   "%ch",   "%dh",   "%bh",
	"%r8b", "%r9b", "%r10b", "%r11b", "%r12b", "%r13b", "%r14b", "%r15b"
};

const char *const dis_REG8_REX[16] = {
	"%al",  "%cl",  "%dl",   "%bl",   "%spl",  "%bpl",  "%sil",  "%dil",
	"%r8b", "%r9b", "%r10b", "%r11b", "%r12b", "%r13b", "%r14b", "%r15b"
};

const char *const dis_REG16[16] = {
	"%ax",  "%cx",  "%dx",   "%bx",   "%sp",   "%bp",   "%si",   "%di",
	"%r8w", "%r9w", "%r10w", "%r11w", "%r12w", "%r13w", "%r14w", "%r15w"
};

const char *const dis_REG32[16] = {
	"%eax", "%ecx", "%edx",  "%ebx",  "%esp",  "%ebp",  "%esi",  "%edi",
	"%r8d", "%r9d", "%r10d", "%r11d", "%r12d", "%r13d", "%r14d", "%r15d"
};

const char *const dis_REG64[16] = {
	"%rax", "%rcx", "%rdx",  "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
	"%r8",  "%r9",  "%r10",  "%r11", "%r12", "%r13", "%r14", "%r15"
};

const char *const dis_DEBUGREG[16] = {
	"%db0", "%db1", "%db2",  "%db3",  "%db4",  "%db5",  "%db6",  "%db7",
	"%db8", "%db9", "%db10", "%db11", "%db12", "%db13", "%db14", "%db15"
};

const char *const dis_CONTROLREG[16] = {
    "%cr0", "%cr1", "%cr2", "%cr3", "%cr4", "%cr5?", "%cr6?", "%cr7?",
    "%cr8", "%cr9?", "%cr10?", "%cr11?", "%cr12?", "%cr13?", "%cr14?", "%cr15?"
};

const char *const dis_TESTREG[16] = {
	"%tr0?", "%tr1?", "%tr2?", "%tr3", "%tr4", "%tr5", "%tr6", "%tr7",
	"%tr0?", "%tr1?", "%tr2?", "%tr3", "%tr4", "%tr5", "%tr6", "%tr7"
};

const char *const dis_MMREG[16] = {
	"%mm0", "%mm1", "%mm2", "%mm3", "%mm4", "%mm5", "%mm6", "%mm7",
	"%mm0", "%mm1", "%mm2", "%mm3", "%mm4", "%mm5", "%mm6", "%mm7"
};

const char *const dis_XMMREG[16] = {
    "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7",
    "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15"
};

const char *const dis_YMMREG[16] = {
    "%ymm0", "%ymm1", "%ymm2", "%ymm3", "%ymm4", "%ymm5", "%ymm6", "%ymm7",
    "%ymm8", "%ymm9", "%ymm10", "%ymm11", "%ymm12", "%ymm13", "%ymm14", "%ymm15"
};

const char *const dis_SEGREG[16] = {
	"%es", "%cs", "%ss", "%ds", "%fs", "%gs", "<reserved>", "<reserved>",
	"%es", "%cs", "%ss", "%ds", "%fs", "%gs", "<reserved>", "<reserved>"
};

/*
 * SIMD predicate suffixes
 */
const char *const dis_PREDSUFFIX[8] = {
	"eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord"
};

const char *const dis_AVXvgrp7[3][8] = {
	/*0	1	2		3		4		5	6		7*/
/*71*/	{"",	"",	"vpsrlw",	"",		"vpsraw",	"",	"vpsllw",	""},
/*72*/	{"",	"",	"vpsrld",	"",		"vpsrad",	"",	"vpslld",	""},
/*73*/	{"",	"",	"vpsrlq",	"vpsrldq",	"",		"",	"vpsllq",	"vpslldq"}
};

#endif	/* DIS_TEXT */

/*
 *	"decode table" for 64 bit mode MOVSXD instruction (opcode 0x63)
 */
const instable_t dis_opMOVSLD = TNS("movslq",MOVSXZ);

/*
 *	"decode table" for pause and clflush instructions
 */
const instable_t dis_opPause = TNS("pause", NORM);

/*
 *	Decode table for 0x0F00 opcodes
 */
const instable_t dis_op0F00[8] = {

/*  [0]  */	TNS("sldt",M),		TNS("str",M),		TNSy("lldt",M), 	TNSy("ltr",M),
/*  [4]  */	TNSZ("verr",M,2),	TNSZ("verw",M,2),	INVALID,		INVALID,
};


/*
 *	Decode table for 0x0F01 opcodes
 */
const instable_t dis_op0F01[8] = {

/*  [0]  */	TNSZ("sgdt",VMx,6),	TNSZ("sidt",MONITOR_MWAIT,6),	TNSZ("lgdt",XGETBV_XSETBV,6),	TNSZ("lidt",SVM,6),
/*  [4]  */	TNSZ("smsw",M,2),	INVALID, 		TNSZ("lmsw",M,2),	TNS("invlpg",SWAPGS_RDTSCP),
};

/*
 *	Decode table for 0x0F18 opcodes -- SIMD prefetch
 */
const instable_t dis_op0F18[8] = {

/*  [0]  */	TNS("prefetchnta",PREF),TNS("prefetcht0",PREF),	TNS("prefetcht1",PREF),	TNS("prefetcht2",PREF),
/*  [4]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

/*
 * 	Decode table for 0x0FAE opcodes -- SIMD state save/restore
 */
const instable_t dis_op0FAE[8] = {
/*  [0]  */	TNSZ("fxsave",M,512),	TNSZ("fxrstor",M,512),	TNS("ldmxcsr",M),	TNS("stmxcsr",M),
/*  [4]  */	TNSZ("xsave",M,512),	TNS("lfence",XMMFENCE), TNS("mfence",XMMFENCE),	TNS("sfence",XMMSFNC),
};

/*
 *	Decode table for 0x0FBA opcodes
 */

const instable_t dis_op0FBA[8] = {

/*  [0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4]  */	TS("bt",MIb),		TS("bts",MIb),		TS("btr",MIb),		TS("btc",MIb),
};

/*
 * 	Decode table for 0x0FC7 opcode (group 9)
 */

const instable_t dis_op0FC7[8] = {

/*  [0]  */	INVALID,		TNS("cmpxchg8b",M),	INVALID,		INVALID,
/*  [4]  */	INVALID,		INVALID,		TNS("vmptrld",MG9),	TNS("vmptrst",MG9),
};

/*
 * 	Decode table for 0x0FC7 opcode (group 9) mode 3
 */

const instable_t dis_op0FC7m3[8] = {

/*  [0]  */	INVALID,		INVALID,	INVALID,		INVALID,
/*  [4]  */	INVALID,		INVALID,	TNS("rdrand",MG9),	TNS("rdseed", MG9),
};

/*
 * 	Decode table for 0x0FC7 opcode with 0x66 prefix
 */

const instable_t dis_op660FC7[8] = {

/*  [0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4]  */	INVALID,		INVALID,		TNS("vmclear",M),	INVALID,
};

/*
 * 	Decode table for 0x0FC7 opcode with 0xF3 prefix
 */

const instable_t dis_opF30FC7[8] = {

/*  [0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4]  */	INVALID,		INVALID,		TNS("vmxon",M),		INVALID,
};

/*
 *	Decode table for 0x0FC8 opcode -- 486 bswap instruction
 *
 *bit pattern: 0000 1111 1100 1reg
 */
const instable_t dis_op0FC8[4] = {
/*  [0]  */	TNS("bswap",R),		INVALID,		INVALID,		INVALID,
};

/*
 *	Decode table for 0x0F71, 0x0F72, and 0x0F73 opcodes -- MMX instructions
 */
const instable_t dis_op0F7123[4][8] = {
{
/*  [70].0 */	INVALID,		INVALID,		INVALID,		INVALID,
/*      .4 */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [71].0 */	INVALID,		INVALID,		TNS("psrlw",MMOSH),	INVALID,
/*      .4 */	TNS("psraw",MMOSH),	INVALID,		TNS("psllw",MMOSH),	INVALID,
}, {
/*  [72].0 */	INVALID,		INVALID,		TNS("psrld",MMOSH),	INVALID,
/*      .4 */	TNS("psrad",MMOSH),	INVALID,		TNS("pslld",MMOSH),	INVALID,
}, {
/*  [73].0 */	INVALID,		INVALID,		TNS("psrlq",MMOSH),	TNS("INVALID",MMOSH),
/*      .4 */	INVALID,		INVALID, 		TNS("psllq",MMOSH),	TNS("INVALID",MMOSH),
} };

/*
 *	Decode table for SIMD extensions to above 0x0F71-0x0F73 opcodes.
 */
const instable_t dis_opSIMD7123[32] = {
/* [70].0 */	INVALID,		INVALID,		INVALID,		INVALID,
/*     .4 */	INVALID,		INVALID,		INVALID,		INVALID,

/* [71].0 */	INVALID,		INVALID,		TNS("psrlw",XMMSH),	INVALID,
/*     .4 */	TNS("psraw",XMMSH),	INVALID,		TNS("psllw",XMMSH),	INVALID,

/* [72].0 */	INVALID,		INVALID,		TNS("psrld",XMMSH),	INVALID,
/*     .4 */	TNS("psrad",XMMSH),	INVALID,		TNS("pslld",XMMSH),	INVALID,

/* [73].0 */	INVALID,		INVALID,		TNS("psrlq",XMMSH),	TNS("psrldq",XMMSH),
/*     .4 */	INVALID,		INVALID,		TNS("psllq",XMMSH),	TNS("pslldq",XMMSH),
};

/*
 *	SIMD instructions have been wedged into the existing IA32 instruction
 *	set through the use of prefixes.  That is, while 0xf0 0x58 may be
 *	addps, 0xf3 0xf0 0x58 (literally, repz addps) is a completely different
 *	instruction - addss.  At present, three prefixes have been coopted in
 *	this manner - address size (0x66), repnz (0xf2) and repz (0xf3).  The
 *	following tables are used to provide the prefixed instruction names.
 *	The arrays are sparse, but they're fast.
 */

/*
 *	Decode table for SIMD instructions with the address size (0x66) prefix.
 */
const instable_t dis_opSIMDdata16[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	TNSZ("movupd",XMM,16),	TNSZ("movupd",XMMS,16),	TNSZ("movlpd",XMMM,8),	TNSZ("movlpd",XMMMS,8),
/*  [14]  */	TNSZ("unpcklpd",XMM,16),TNSZ("unpckhpd",XMM,16),TNSZ("movhpd",XMMM,8),	TNSZ("movhpd",XMMMS,8),
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	TNSZ("movapd",XMM,16),	TNSZ("movapd",XMMS,16),	TNSZ("cvtpi2pd",XMMOMX,8),TNSZ("movntpd",XMMOMS,16),
/*  [2C]  */	TNSZ("cvttpd2pi",XMMXMM,16),TNSZ("cvtpd2pi",XMMXMM,16),TNSZ("ucomisd",XMM,8),TNSZ("comisd",XMM,8),

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	TNS("movmskpd",XMMOX3),	TNSZ("sqrtpd",XMM,16),	INVALID,		INVALID,
/*  [54]  */	TNSZ("andpd",XMM,16),	TNSZ("andnpd",XMM,16),	TNSZ("orpd",XMM,16),	TNSZ("xorpd",XMM,16),
/*  [58]  */	TNSZ("addpd",XMM,16),	TNSZ("mulpd",XMM,16),	TNSZ("cvtpd2ps",XMM,16),TNSZ("cvtps2dq",XMM,16),
/*  [5C]  */	TNSZ("subpd",XMM,16),	TNSZ("minpd",XMM,16),	TNSZ("divpd",XMM,16),	TNSZ("maxpd",XMM,16),

/*  [60]  */	TNSZ("punpcklbw",XMM,16),TNSZ("punpcklwd",XMM,16),TNSZ("punpckldq",XMM,16),TNSZ("packsswb",XMM,16),
/*  [64]  */	TNSZ("pcmpgtb",XMM,16),	TNSZ("pcmpgtw",XMM,16),	TNSZ("pcmpgtd",XMM,16),	TNSZ("packuswb",XMM,16),
/*  [68]  */	TNSZ("punpckhbw",XMM,16),TNSZ("punpckhwd",XMM,16),TNSZ("punpckhdq",XMM,16),TNSZ("packssdw",XMM,16),
/*  [6C]  */	TNSZ("punpcklqdq",XMM,16),TNSZ("punpckhqdq",XMM,16),TNSZ("movd",XMM3MX,4),TNSZ("movdqa",XMM,16),

/*  [70]  */	TNSZ("pshufd",XMMP,16),	INVALID,		INVALID,		INVALID,
/*  [74]  */	TNSZ("pcmpeqb",XMM,16),	TNSZ("pcmpeqw",XMM,16),	TNSZ("pcmpeqd",XMM,16),	INVALID,
/*  [78]  */	TNSZ("extrq",XMM2I,16),	TNSZ("extrq",XMM,16), INVALID,		INVALID,
/*  [7C]  */	TNSZ("haddpd",XMM,16),	TNSZ("hsubpd",XMM,16),	TNSZ("movd",XMM3MXS,4),	TNSZ("movdqa",XMMS,16),

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [8C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		TNSZ("cmppd",XMMP,16),	INVALID,
/*  [C4]  */	TNSZ("pinsrw",XMMPRM,2),TNS("pextrw",XMM3P),	TNSZ("shufpd",XMMP,16),	INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	TNSZ("addsubpd",XMM,16),TNSZ("psrlw",XMM,16),	TNSZ("psrld",XMM,16),	TNSZ("psrlq",XMM,16),
/*  [D4]  */	TNSZ("paddq",XMM,16),	TNSZ("pmullw",XMM,16),	TNSZ("movq",XMMS,8),	TNS("pmovmskb",XMMX3),
/*  [D8]  */	TNSZ("psubusb",XMM,16),	TNSZ("psubusw",XMM,16),	TNSZ("pminub",XMM,16),	TNSZ("pand",XMM,16),
/*  [DC]  */	TNSZ("paddusb",XMM,16),	TNSZ("paddusw",XMM,16),	TNSZ("pmaxub",XMM,16),	TNSZ("pandn",XMM,16),

/*  [E0]  */	TNSZ("pavgb",XMM,16),	TNSZ("psraw",XMM,16),	TNSZ("psrad",XMM,16),	TNSZ("pavgw",XMM,16),
/*  [E4]  */	TNSZ("pmulhuw",XMM,16),	TNSZ("pmulhw",XMM,16),	TNSZ("cvttpd2dq",XMM,16),TNSZ("movntdq",XMMS,16),
/*  [E8]  */	TNSZ("psubsb",XMM,16),	TNSZ("psubsw",XMM,16),	TNSZ("pminsw",XMM,16),	TNSZ("por",XMM,16),
/*  [EC]  */	TNSZ("paddsb",XMM,16),	TNSZ("paddsw",XMM,16),	TNSZ("pmaxsw",XMM,16),	TNSZ("pxor",XMM,16),

/*  [F0]  */	INVALID,		TNSZ("psllw",XMM,16),	TNSZ("pslld",XMM,16),	TNSZ("psllq",XMM,16),
/*  [F4]  */	TNSZ("pmuludq",XMM,16),	TNSZ("pmaddwd",XMM,16),	TNSZ("psadbw",XMM,16),	TNSZ("maskmovdqu", XMMXIMPL,16),
/*  [F8]  */	TNSZ("psubb",XMM,16),	TNSZ("psubw",XMM,16),	TNSZ("psubd",XMM,16),	TNSZ("psubq",XMM,16),
/*  [FC]  */	TNSZ("paddb",XMM,16),	TNSZ("paddw",XMM,16),	TNSZ("paddd",XMM,16),	INVALID,
};

const instable_t dis_opAVX660F[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	TNSZ("vmovupd",VEX_MX,16),	TNSZ("vmovupd",VEX_RX,16),	TNSZ("vmovlpd",VEX_RMrX,8),	TNSZ("vmovlpd",VEX_RM,8),
/*  [14]  */	TNSZ("vunpcklpd",VEX_RMrX,16),TNSZ("vunpckhpd",VEX_RMrX,16),TNSZ("vmovhpd",VEX_RMrX,8),	TNSZ("vmovhpd",VEX_RM,8),
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	TNSZ("vmovapd",VEX_MX,16),	TNSZ("vmovapd",VEX_RX,16),	INVALID,		TNSZ("vmovntpd",VEX_RM,16),
/*  [2C]  */	INVALID,		INVALID,		TNSZ("vucomisd",VEX_MX,8),TNSZ("vcomisd",VEX_MX,8),

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	TNS("vmovmskpd",VEX_MR),	TNSZ("vsqrtpd",VEX_MX,16),	INVALID,		INVALID,
/*  [54]  */	TNSZ("vandpd",VEX_RMrX,16),	TNSZ("vandnpd",VEX_RMrX,16),	TNSZ("vorpd",VEX_RMrX,16),	TNSZ("vxorpd",VEX_RMrX,16),
/*  [58]  */	TNSZ("vaddpd",VEX_RMrX,16),	TNSZ("vmulpd",VEX_RMrX,16),	TNSZ("vcvtpd2ps",VEX_MX,16),TNSZ("vcvtps2dq",VEX_MX,16),
/*  [5C]  */	TNSZ("vsubpd",VEX_RMrX,16),	TNSZ("vminpd",VEX_RMrX,16),	TNSZ("vdivpd",VEX_RMrX,16),	TNSZ("vmaxpd",VEX_RMrX,16),

/*  [60]  */	TNSZ("vpunpcklbw",VEX_RMrX,16),TNSZ("vpunpcklwd",VEX_RMrX,16),TNSZ("vpunpckldq",VEX_RMrX,16),TNSZ("vpacksswb",VEX_RMrX,16),
/*  [64]  */	TNSZ("vpcmpgtb",VEX_RMrX,16),	TNSZ("vpcmpgtw",VEX_RMrX,16),	TNSZ("vpcmpgtd",VEX_RMrX,16),	TNSZ("vpackuswb",VEX_RMrX,16),
/*  [68]  */	TNSZ("vpunpckhbw",VEX_RMrX,16),TNSZ("vpunpckhwd",VEX_RMrX,16),TNSZ("vpunpckhdq",VEX_RMrX,16),TNSZ("vpackssdw",VEX_RMrX,16),
/*  [6C]  */	TNSZ("vpunpcklqdq",VEX_RMrX,16),TNSZ("vpunpckhqdq",VEX_RMrX,16),TNSZ("vmovd",VEX_MX,4),TNSZ("vmovdqa",VEX_MX,16),

/*  [70]  */	TNSZ("vpshufd",VEX_MXI,16),	TNSZ("vgrp71",VEX_XXI,16),	TNSZ("vgrp72",VEX_XXI,16),		TNSZ("vgrp73",VEX_XXI,16),
/*  [74]  */	TNSZ("vpcmpeqb",VEX_RMrX,16),	TNSZ("vpcmpeqw",VEX_RMrX,16),	TNSZ("vpcmpeqd",VEX_RMrX,16),	INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	TNSZ("vhaddpd",VEX_RMrX,16),	TNSZ("vhsubpd",VEX_RMrX,16),	TNSZ("vmovd",VEX_RR,4),	TNSZ("vmovdqa",VEX_RX,16),

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [8C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		TNSZ("vcmppd",VEX_RMRX,16),	INVALID,
/*  [C4]  */	TNSZ("vpinsrw",VEX_RMRX,2),TNS("vpextrw",VEX_MR),	TNSZ("vshufpd",VEX_RMRX,16),	INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	TNSZ("vaddsubpd",VEX_RMrX,16),TNSZ("vpsrlw",VEX_RMrX,16),	TNSZ("vpsrld",VEX_RMrX,16),	TNSZ("vpsrlq",VEX_RMrX,16),
/*  [D4]  */	TNSZ("vpaddq",VEX_RMrX,16),	TNSZ("vpmullw",VEX_RMrX,16),	TNSZ("vmovq",VEX_RX,8),	TNS("vpmovmskb",VEX_MR),
/*  [D8]  */	TNSZ("vpsubusb",VEX_RMrX,16),	TNSZ("vpsubusw",VEX_RMrX,16),	TNSZ("vpminub",VEX_RMrX,16),	TNSZ("vpand",VEX_RMrX,16),
/*  [DC]  */	TNSZ("vpaddusb",VEX_RMrX,16),	TNSZ("vpaddusw",VEX_RMrX,16),	TNSZ("vpmaxub",VEX_RMrX,16),	TNSZ("vpandn",VEX_RMrX,16),

/*  [E0]  */	TNSZ("vpavgb",VEX_RMrX,16),	TNSZ("vpsraw",VEX_RMrX,16),	TNSZ("vpsrad",VEX_RMrX,16),	TNSZ("vpavgw",VEX_RMrX,16),
/*  [E4]  */	TNSZ("vpmulhuw",VEX_RMrX,16),	TNSZ("vpmulhw",VEX_RMrX,16),	TNSZ("vcvttpd2dq",VEX_MX,16),TNSZ("vmovntdq",VEX_RM,16),
/*  [E8]  */	TNSZ("vpsubsb",VEX_RMrX,16),	TNSZ("vpsubsw",VEX_RMrX,16),	TNSZ("vpminsw",VEX_RMrX,16),	TNSZ("vpor",VEX_RMrX,16),
/*  [EC]  */	TNSZ("vpaddsb",VEX_RMrX,16),	TNSZ("vpaddsw",VEX_RMrX,16),	TNSZ("vpmaxsw",VEX_RMrX,16),	TNSZ("vpxor",VEX_RMrX,16),

/*  [F0]  */	INVALID,		TNSZ("vpsllw",VEX_RMrX,16),	TNSZ("vpslld",VEX_RMrX,16),	TNSZ("vpsllq",VEX_RMrX,16),
/*  [F4]  */	TNSZ("vpmuludq",VEX_RMrX,16),	TNSZ("vpmaddwd",VEX_RMrX,16),	TNSZ("vpsadbw",VEX_RMrX,16),	TNS("vmaskmovdqu",VEX_MX),
/*  [F8]  */	TNSZ("vpsubb",VEX_RMrX,16),	TNSZ("vpsubw",VEX_RMrX,16),	TNSZ("vpsubd",VEX_RMrX,16),	TNSZ("vpsubq",VEX_RMrX,16),
/*  [FC]  */	TNSZ("vpaddb",VEX_RMrX,16),	TNSZ("vpaddw",VEX_RMrX,16),	TNSZ("vpaddd",VEX_RMrX,16),	INVALID,
};

/*
 *	Decode table for SIMD instructions with the repnz (0xf2) prefix.
 */
const instable_t dis_opSIMDrepnz[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	TNSZ("movsd",XMM,8),	TNSZ("movsd",XMMS,8),	TNSZ("movddup",XMM,8),	INVALID,
/*  [14]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		TNSZ("cvtsi2sd",XMM3MX,4),TNSZ("movntsd",XMMMS,8),
/*  [2C]  */	TNSZ("cvttsd2si",XMMXM3,8),TNSZ("cvtsd2si",XMMXM3,8),INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		TNSZ("sqrtsd",XMM,8),	INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	TNSZ("addsd",XMM,8),	TNSZ("mulsd",XMM,8),	TNSZ("cvtsd2ss",XMM,8),	INVALID,
/*  [5C]  */	TNSZ("subsd",XMM,8),	TNSZ("minsd",XMM,8),	TNSZ("divsd",XMM,8),	TNSZ("maxsd",XMM,8),

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	TNSZ("pshuflw",XMMP,16),INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	TNSZ("insertq",XMMX2I,16),TNSZ("insertq",XMM,8),INVALID,		INVALID,
/*  [7C]  */	TNSZ("haddps",XMM,16),	TNSZ("hsubps",XMM,16),	INVALID,		INVALID,

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		TNSZ("cmpsd",XMMP,8),	INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	TNSZ("addsubps",XMM,16),INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		TNS("movdq2q",XMMXM),	INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		TNSZ("cvtpd2dq",XMM,16),INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	TNS("lddqu",XMMM),	INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

const instable_t dis_opAVXF20F[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	TNSZ("vmovsd",VEX_RMrX,8),	TNSZ("vmovsd",VEX_RRX,8),	TNSZ("vmovddup",VEX_MX,8),	INVALID,
/*  [14]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		TNSZ("vcvtsi2sd",VEX_RMrX,4),INVALID,
/*  [2C]  */	TNSZ("vcvttsd2si",VEX_MR,8),TNSZ("vcvtsd2si",VEX_MR,8),INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		TNSZ("vsqrtsd",VEX_RMrX,8),	INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	TNSZ("vaddsd",VEX_RMrX,8),	TNSZ("vmulsd",VEX_RMrX,8),	TNSZ("vcvtsd2ss",VEX_RMrX,8),	INVALID,
/*  [5C]  */	TNSZ("vsubsd",VEX_RMrX,8),	TNSZ("vminsd",VEX_RMrX,8),	TNSZ("vdivsd",VEX_RMrX,8),	TNSZ("vmaxsd",VEX_RMrX,8),

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	TNSZ("vpshuflw",VEX_MXI,16),INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	TNSZ("vhaddps",VEX_RMrX,8),	TNSZ("vhsubps",VEX_RMrX,8),	INVALID,		INVALID,

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		TNSZ("vcmpsd",VEX_RMRX,8),	INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	TNSZ("vaddsubps",VEX_RMrX,8),	INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		TNSZ("vcvtpd2dq",VEX_MX,16),INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	TNSZ("vlddqu",VEX_MX,16),	INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

const instable_t dis_opAVXF20F3A[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [14]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [2C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [5C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	TNSZvr("rorx",VEX_MXI,6),INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

const instable_t dis_opAVXF20F38[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [14]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [2C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [5C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		TNSZvr("pdep",VEX_RMrX,5),TNSZvr("mulx",VEX_RMrX,5),TNSZvr("shrx",VEX_VRMrX,5),
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

const instable_t dis_opAVXF30F38[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [14]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [2C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [5C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		TNSZvr("pext",VEX_RMrX,5),INVALID,		TNSZvr("sarx",VEX_VRMrX,5),
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};
/*
 *	Decode table for SIMD instructions with the repz (0xf3) prefix.
 */
const instable_t dis_opSIMDrepz[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	TNSZ("movss",XMM,4),	TNSZ("movss",XMMS,4),	TNSZ("movsldup",XMM,16),INVALID,
/*  [14]  */	INVALID,		INVALID,		TNSZ("movshdup",XMM,16),INVALID,
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		TNSZ("cvtsi2ss",XMM3MX,4),TNSZ("movntss",XMMMS,4),
/*  [2C]  */	TNSZ("cvttss2si",XMMXM3,4),TNSZ("cvtss2si",XMMXM3,4),INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		TNSZ("sqrtss",XMM,4),	TNSZ("rsqrtss",XMM,4),	TNSZ("rcpss",XMM,4),
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	TNSZ("addss",XMM,4),	TNSZ("mulss",XMM,4),	TNSZ("cvtss2sd",XMM,4),	TNSZ("cvttps2dq",XMM,16),
/*  [5C]  */	TNSZ("subss",XMM,4),	TNSZ("minss",XMM,4),	TNSZ("divss",XMM,4),	TNSZ("maxss",XMM,4),

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		TNSZ("movdqu",XMM,16),

/*  [70]  */	TNSZ("pshufhw",XMMP,16),INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		TNSZ("movq",XMM,8),	TNSZ("movdqu",XMMS,16),

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	TS("popcnt",MRw),	INVALID,		INVALID,		INVALID,
/*  [BC]  */	TNSZ("tzcnt",MRw,5),	TS("lzcnt",MRw),	INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		TNSZ("cmpss",XMMP,4),	INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		TNS("movq2dq",XMMMX),	INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		TNSZ("cvtdq2pd",XMM,8),	INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

const instable_t dis_opAVXF30F[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	TNSZ("vmovss",VEX_RMrX,4),	TNSZ("vmovss",VEX_RRX,4),	TNSZ("vmovsldup",VEX_MX,4),	INVALID,
/*  [14]  */	INVALID,		INVALID,		TNSZ("vmovshdup",VEX_MX,4),	INVALID,
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		TNSZ("vcvtsi2ss",VEX_RMrX,4),INVALID,
/*  [2C]  */	TNSZ("vcvttss2si",VEX_MR,4),TNSZ("vcvtss2si",VEX_MR,4),INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		TNSZ("vsqrtss",VEX_RMrX,4),	TNSZ("vrsqrtss",VEX_RMrX,4),	TNSZ("vrcpss",VEX_RMrX,4),
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	TNSZ("vaddss",VEX_RMrX,4),	TNSZ("vmulss",VEX_RMrX,4),	TNSZ("vcvtss2sd",VEX_RMrX,4),	TNSZ("vcvttps2dq",VEX_MX,16),
/*  [5C]  */	TNSZ("vsubss",VEX_RMrX,4),	TNSZ("vminss",VEX_RMrX,4),	TNSZ("vdivss",VEX_RMrX,4),	TNSZ("vmaxss",VEX_RMrX,4),

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		TNSZ("vmovdqu",VEX_MX,16),

/*  [70]  */	TNSZ("vpshufhw",VEX_MXI,16),INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		TNSZ("vmovq",VEX_MX,8),	TNSZ("vmovdqu",VEX_RX,16),

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		TNSZ("vcmpss",VEX_RMRX,4),	INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		TNSZ("vcvtdq2pd",VEX_MX,8),	INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};
/*
 * The following two tables are used to encode crc32 and movbe
 * since they share the same opcodes.
 */
const instable_t dis_op0F38F0[2] = {
/*  [00]  */	TNS("crc32b",CRC32),
		TS("movbe",MOVBE),
};

const instable_t dis_op0F38F1[2] = {
/*  [00]  */	TS("crc32",CRC32),
		TS("movbe",MOVBE),
};

/*
 * The following table is used to distinguish between adox and adcx which share
 * the same opcodes.
 */
const instable_t dis_op0F38F6[2] = {
/*  [00]  */	TNS("adcx",ADX),
		TNS("adox",ADX),
};

const instable_t dis_op0F38[256] = {
/*  [00]  */	TNSZ("pshufb",XMM_66o,16),TNSZ("phaddw",XMM_66o,16),TNSZ("phaddd",XMM_66o,16),TNSZ("phaddsw",XMM_66o,16),
/*  [04]  */	TNSZ("pmaddubsw",XMM_66o,16),TNSZ("phsubw",XMM_66o,16),	TNSZ("phsubd",XMM_66o,16),TNSZ("phsubsw",XMM_66o,16),
/*  [08]  */	TNSZ("psignb",XMM_66o,16),TNSZ("psignw",XMM_66o,16),TNSZ("psignd",XMM_66o,16),TNSZ("pmulhrsw",XMM_66o,16),
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [10]  */	TNSZ("pblendvb",XMM_66r,16),INVALID,		INVALID,		INVALID,
/*  [14]  */	TNSZ("blendvps",XMM_66r,16),TNSZ("blendvpd",XMM_66r,16),INVALID,	TNSZ("ptest",XMM_66r,16),
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	TNSZ("pabsb",XMM_66o,16),TNSZ("pabsw",XMM_66o,16),TNSZ("pabsd",XMM_66o,16),INVALID,

/*  [20]  */	TNSZ("pmovsxbw",XMM_66r,16),TNSZ("pmovsxbd",XMM_66r,16),TNSZ("pmovsxbq",XMM_66r,16),TNSZ("pmovsxwd",XMM_66r,16),
/*  [24]  */	TNSZ("pmovsxwq",XMM_66r,16),TNSZ("pmovsxdq",XMM_66r,16),INVALID,	INVALID,
/*  [28]  */	TNSZ("pmuldq",XMM_66r,16),TNSZ("pcmpeqq",XMM_66r,16),TNSZ("movntdqa",XMMM_66r,16),TNSZ("packusdw",XMM_66r,16),
/*  [2C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [30]  */	TNSZ("pmovzxbw",XMM_66r,16),TNSZ("pmovzxbd",XMM_66r,16),TNSZ("pmovzxbq",XMM_66r,16),TNSZ("pmovzxwd",XMM_66r,16),
/*  [34]  */	TNSZ("pmovzxwq",XMM_66r,16),TNSZ("pmovzxdq",XMM_66r,16),INVALID,	TNSZ("pcmpgtq",XMM_66r,16),
/*  [38]  */	TNSZ("pminsb",XMM_66r,16),TNSZ("pminsd",XMM_66r,16),TNSZ("pminuw",XMM_66r,16),TNSZ("pminud",XMM_66r,16),
/*  [3C]  */	TNSZ("pmaxsb",XMM_66r,16),TNSZ("pmaxsd",XMM_66r,16),TNSZ("pmaxuw",XMM_66r,16),TNSZ("pmaxud",XMM_66r,16),

/*  [40]  */	TNSZ("pmulld",XMM_66r,16),TNSZ("phminposuw",XMM_66r,16),INVALID,	INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [5C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [80]  */	TNSy("invept", RM_66r),	TNSy("invvpid", RM_66r),TNSy("invpcid", RM_66r),INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [8C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	TNSZ("sha1nexte",XMM,16),TNSZ("sha1msg1",XMM,16),TNSZ("sha1msg2",XMM,16),TNSZ("sha256rnds2",XMM,16),
/*  [CC]  */	TNSZ("sha256msg1",XMM,16),TNSZ("sha256msg2",XMM,16),INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		TNSZ("aesimc",XMM_66r,16),
/*  [DC]  */	TNSZ("aesenc",XMM_66r,16),TNSZ("aesenclast",XMM_66r,16),TNSZ("aesdec",XMM_66r,16),TNSZ("aesdeclast",XMM_66r,16),

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F0]  */	IND(dis_op0F38F0),	IND(dis_op0F38F1),	INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		IND(dis_op0F38F6),	INVALID,
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

const instable_t dis_opAVX660F38[256] = {
/*  [00]  */	TNSZ("vpshufb",VEX_RMrX,16),TNSZ("vphaddw",VEX_RMrX,16),TNSZ("vphaddd",VEX_RMrX,16),TNSZ("vphaddsw",VEX_RMrX,16),
/*  [04]  */	TNSZ("vpmaddubsw",VEX_RMrX,16),TNSZ("vphsubw",VEX_RMrX,16),	TNSZ("vphsubd",VEX_RMrX,16),TNSZ("vphsubsw",VEX_RMrX,16),
/*  [08]  */	TNSZ("vpsignb",VEX_RMrX,16),TNSZ("vpsignw",VEX_RMrX,16),TNSZ("vpsignd",VEX_RMrX,16),TNSZ("vpmulhrsw",VEX_RMrX,16),
/*  [0C]  */	TNSZ("vpermilps",VEX_RMrX,8),TNSZ("vpermilpd",VEX_RMrX,16),TNSZ("vtestps",VEX_RRI,8),	TNSZ("vtestpd",VEX_RRI,16),

/*  [10]  */	INVALID,		INVALID,		INVALID,		TNSZ("vcvtph2ps",VEX_MX,16),
/*  [14]  */	INVALID,		INVALID,		TNSZ("vpermps",VEX_RMrX,16),TNSZ("vptest",VEX_RRI,16),
/*  [18]  */	TNSZ("vbroadcastss",VEX_MX,4),TNSZ("vbroadcastsd",VEX_MX,8),TNSZ("vbroadcastf128",VEX_MX,16),INVALID,
/*  [1C]  */	TNSZ("vpabsb",VEX_MX,16),TNSZ("vpabsw",VEX_MX,16),TNSZ("vpabsd",VEX_MX,16),INVALID,

/*  [20]  */	TNSZ("vpmovsxbw",VEX_MX,16),TNSZ("vpmovsxbd",VEX_MX,16),TNSZ("vpmovsxbq",VEX_MX,16),TNSZ("vpmovsxwd",VEX_MX,16),
/*  [24]  */	TNSZ("vpmovsxwq",VEX_MX,16),TNSZ("vpmovsxdq",VEX_MX,16),INVALID,	INVALID,
/*  [28]  */	TNSZ("vpmuldq",VEX_RMrX,16),TNSZ("vpcmpeqq",VEX_RMrX,16),TNSZ("vmovntdqa",VEX_MX,16),TNSZ("vpackusdw",VEX_RMrX,16),
/*  [2C]  */	TNSZ("vmaskmovps",VEX_RMrX,8),TNSZ("vmaskmovpd",VEX_RMrX,16),TNSZ("vmaskmovps",VEX_RRM,8),TNSZ("vmaskmovpd",VEX_RRM,16),

/*  [30]  */	TNSZ("vpmovzxbw",VEX_MX,16),TNSZ("vpmovzxbd",VEX_MX,16),TNSZ("vpmovzxbq",VEX_MX,16),TNSZ("vpmovzxwd",VEX_MX,16),
/*  [34]  */	TNSZ("vpmovzxwq",VEX_MX,16),TNSZ("vpmovzxdq",VEX_MX,16),TNSZ("vpermd",VEX_RMrX,16),TNSZ("vpcmpgtq",VEX_RMrX,16),
/*  [38]  */	TNSZ("vpminsb",VEX_RMrX,16),TNSZ("vpminsd",VEX_RMrX,16),TNSZ("vpminuw",VEX_RMrX,16),TNSZ("vpminud",VEX_RMrX,16),
/*  [3C]  */	TNSZ("vpmaxsb",VEX_RMrX,16),TNSZ("vpmaxsd",VEX_RMrX,16),TNSZ("vpmaxuw",VEX_RMrX,16),TNSZ("vpmaxud",VEX_RMrX,16),

/*  [40]  */	TNSZ("vpmulld",VEX_RMrX,16),TNSZ("vphminposuw",VEX_MX,16),INVALID,	INVALID,
/*  [44]  */	INVALID,		TSaZ("vpsrlv",VEX_RMrX,16),TNSZ("vpsravd",VEX_RMrX,16),TSaZ("vpsllv",VEX_RMrX,16),
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	TNSZ("vpbroadcastd",VEX_MX,16),TNSZ("vpbroadcastq",VEX_MX,16),TNSZ("vbroadcasti128",VEX_MX,16),INVALID,
/*  [5C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	TNSZ("vpbroadcastb",VEX_MX,16),TNSZ("vpbroadcastw",VEX_MX,16),INVALID,	INVALID,
/*  [7C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [8C]  */	TSaZ("vpmaskmov",VEX_RMrX,16),INVALID,		TSaZ("vpmaskmov",VEX_RRM,16),INVALID,

/*  [90]  */	TNSZ("vpgatherd",VEX_SbVM,16),TNSZ("vpgatherq",VEX_SbVM,16),TNSZ("vgatherdp",VEX_SbVM,16),TNSZ("vgatherqp",VEX_SbVM,16),
/*  [94]  */	INVALID,		INVALID,		TNSZ("vfmaddsub132p",FMA,16),TNSZ("vfmsubadd132p",FMA,16),
/*  [98]  */	TNSZ("vfmadd132p",FMA,16),TNSZ("vfmadd132s",FMA,16),TNSZ("vfmsub132p",FMA,16),TNSZ("vfmsub132s",FMA,16),
/*  [9C]  */	TNSZ("vfnmadd132p",FMA,16),TNSZ("vfnmadd132s",FMA,16),TNSZ("vfnmsub132p",FMA,16),TNSZ("vfnmsub132s",FMA,16),

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		TNSZ("vfmaddsub213p",FMA,16),TNSZ("vfmsubadd213p",FMA,16),
/*  [A8]  */	TNSZ("vfmadd213p",FMA,16),TNSZ("vfmadd213s",FMA,16),TNSZ("vfmsub213p",FMA,16),TNSZ("vfmsub213s",FMA,16),
/*  [AC]  */	TNSZ("vfnmadd213p",FMA,16),TNSZ("vfnmadd213s",FMA,16),TNSZ("vfnmsub213p",FMA,16),TNSZ("vfnmsub213s",FMA,16),

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		TNSZ("vfmaddsub231p",FMA,16),TNSZ("vfmsubadd231p",FMA,16),
/*  [B8]  */	TNSZ("vfmadd231p",FMA,16),TNSZ("vfmadd231s",FMA,16),TNSZ("vfmsub231p",FMA,16),TNSZ("vfmsub231s",FMA,16),
/*  [BC]  */	TNSZ("vfnmadd231p",FMA,16),TNSZ("vfnmadd231s",FMA,16),TNSZ("vfnmsub231p",FMA,16),TNSZ("vfnmsub231s",FMA,16),

/*  [C0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		TNSZ("vaesimc",VEX_MX,16),
/*  [DC]  */	TNSZ("vaesenc",VEX_RMrX,16),TNSZ("vaesenclast",VEX_RMrX,16),TNSZ("vaesdec",VEX_RMrX,16),TNSZ("vaesdeclast",VEX_RMrX,16),

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F0]  */	IND(dis_op0F38F0),	IND(dis_op0F38F1),	INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		INVALID,		TNSZvr("shlx",VEX_VRMrX,5),
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

const instable_t dis_op0F3A[256] = {
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	TNSZ("roundps",XMMP_66r,16),TNSZ("roundpd",XMMP_66r,16),TNSZ("roundss",XMMP_66r,16),TNSZ("roundsd",XMMP_66r,16),
/*  [0C]  */	TNSZ("blendps",XMMP_66r,16),TNSZ("blendpd",XMMP_66r,16),TNSZ("pblendw",XMMP_66r,16),TNSZ("palignr",XMMP_66o,16),

/*  [10]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [14]  */	TNSZ("pextrb",XMM3PM_66r,8),TNSZ("pextrw",XMM3PM_66r,16),TSZ("pextr",XMM3PM_66r,16),TNSZ("extractps",XMM3PM_66r,16),
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [20]  */	TNSZ("pinsrb",XMMPRM_66r,8),TNSZ("insertps",XMMP_66r,16),TSZ("pinsr",XMMPRM_66r,16),INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [2C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	TNSZ("dpps",XMMP_66r,16),TNSZ("dppd",XMMP_66r,16),TNSZ("mpsadbw",XMMP_66r,16),INVALID,
/*  [44]  */	TNSZ("pclmulqdq",XMMP_66r,16),INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [5C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [60]  */	TNSZ("pcmpestrm",XMMP_66r,16),TNSZ("pcmpestri",XMMP_66r,16),TNSZ("pcmpistrm",XMMP_66r,16),TNSZ("pcmpistri",XMMP_66r,16),
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [8C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	TNSZ("sha1rnds4",XMMP,16),INVALID,		INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		TNSZ("aeskeygenassist",XMMP_66r,16),

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

const instable_t dis_opAVX660F3A[256] = {
/*  [00]  */	TNSZ("vpermq",VEX_MXI,16),TNSZ("vpermpd",VEX_MXI,16),TNSZ("vpblendd",VEX_RMRX,16),INVALID,
/*  [04]  */	TNSZ("vpermilps",VEX_MXI,8),TNSZ("vpermilpd",VEX_MXI,16),TNSZ("vperm2f128",VEX_RMRX,16),INVALID,
/*  [08]  */	TNSZ("vroundps",VEX_MXI,16),TNSZ("vroundpd",VEX_MXI,16),TNSZ("vroundss",VEX_RMRX,16),TNSZ("vroundsd",VEX_RMRX,16),
/*  [0C]  */	TNSZ("vblendps",VEX_RMRX,16),TNSZ("vblendpd",VEX_RMRX,16),TNSZ("vpblendw",VEX_RMRX,16),TNSZ("vpalignr",VEX_RMRX,16),

/*  [10]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [14]  */	TNSZ("vpextrb",VEX_RRi,8),TNSZ("vpextrw",VEX_RRi,16),TNSZ("vpextrd",VEX_RRi,16),TNSZ("vextractps",VEX_RM,16),
/*  [18]  */	TNSZ("vinsertf128",VEX_RMRX,16),TNSZ("vextractf128",VEX_RX,16),INVALID,		INVALID,
/*  [1C]  */	INVALID,		TNSZ("vcvtps2ph",VEX_RX,16),		INVALID,		INVALID,

/*  [20]  */	TNSZ("vpinsrb",VEX_RMRX,8),TNSZ("vinsertps",VEX_RMRX,16),TNSZ("vpinsrd",VEX_RMRX,16),INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [2C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	TNSZ("vinserti128",VEX_RMRX,16),TNSZ("vextracti128",VEX_RIM,16),INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [40]  */	TNSZ("vdpps",VEX_RMRX,16),TNSZ("vdppd",VEX_RMRX,16),TNSZ("vmpsadbw",VEX_RMRX,16),INVALID,
/*  [44]  */	TNSZ("vpclmulqdq",VEX_RMRX,16),INVALID,		TNSZ("vperm2i128",VEX_RMRX,16),INVALID,
/*  [48]  */	INVALID,		INVALID,		TNSZ("vblendvps",VEX_RMRX,8),	TNSZ("vblendvpd",VEX_RMRX,16),
/*  [4C]  */	TNSZ("vpblendvb",VEX_RMRX,16),INVALID,		INVALID,		INVALID,

/*  [50]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [54]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [58]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [5C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [60]  */	TNSZ("vpcmpestrm",VEX_MXI,16),TNSZ("vpcmpestri",VEX_MXI,16),TNSZ("vpcmpistrm",VEX_MXI,16),TNSZ("vpcmpistri",VEX_MXI,16),
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [70]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [8C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [C0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		TNSZ("vaeskeygenassist",VEX_MXI,16),

/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,

/*  [F0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

/*
 * 	Decode table for 0x0F0D which uses the first byte of the mod_rm to
 * 	indicate a sub-code.
 */
const instable_t dis_op0F0D[8] = {
/*  [00]  */	INVALID,		TNS("prefetchw",PREF),	TNS("prefetchwt1",PREF),INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

/*
 *	Decode table for 0x0F opcodes
 */

const instable_t dis_op0F[16][16] = {
{
/*  [00]  */	IND(dis_op0F00),	IND(dis_op0F01),	TNS("lar",MR),		TNS("lsl",MR),
/*  [04]  */	INVALID,		TNS("syscall",NORM),	TNS("clts",NORM),	TNS("sysret",NORM),
/*  [08]  */	TNS("invd",NORM),	TNS("wbinvd",NORM),	INVALID,		TNS("ud2",NORM),
/*  [0C]  */	INVALID,		IND(dis_op0F0D),	INVALID,		INVALID,
}, {
/*  [10]  */	TNSZ("movups",XMMO,16),	TNSZ("movups",XMMOS,16),TNSZ("movlps",XMMO,8),	TNSZ("movlps",XMMOS,8),
/*  [14]  */	TNSZ("unpcklps",XMMO,16),TNSZ("unpckhps",XMMO,16),TNSZ("movhps",XMMOM,8),TNSZ("movhps",XMMOMS,8),
/*  [18]  */	IND(dis_op0F18),	INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		TS("nop",Mw),
}, {
/*  [20]  */	TSy("mov",SREG),	TSy("mov",SREG),	TSy("mov",SREG),	TSy("mov",SREG),
/*  [24]  */	TSx("mov",SREG),	INVALID,		TSx("mov",SREG),	INVALID,
/*  [28]  */	TNSZ("movaps",XMMO,16),	TNSZ("movaps",XMMOS,16),TNSZ("cvtpi2ps",XMMOMX,8),TNSZ("movntps",XMMOS,16),
/*  [2C]  */	TNSZ("cvttps2pi",XMMOXMM,8),TNSZ("cvtps2pi",XMMOXMM,8),TNSZ("ucomiss",XMMO,4),TNSZ("comiss",XMMO,4),
}, {
/*  [30]  */	TNS("wrmsr",NORM),	TNS("rdtsc",NORM),	TNS("rdmsr",NORM),	TNS("rdpmc",NORM),
/*  [34]  */	TNSx("sysenter",NORM),	TNSx("sysexit",NORM),	INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [40]  */	TS("cmovx.o",MR),	TS("cmovx.no",MR),	TS("cmovx.b",MR),	TS("cmovx.ae",MR),
/*  [44]  */	TS("cmovx.e",MR),	TS("cmovx.ne",MR),	TS("cmovx.be",MR),	TS("cmovx.a",MR),
/*  [48]  */	TS("cmovx.s",MR),	TS("cmovx.ns",MR),	TS("cmovx.pe",MR),	TS("cmovx.po",MR),
/*  [4C]  */	TS("cmovx.l",MR),	TS("cmovx.ge",MR),	TS("cmovx.le",MR),	TS("cmovx.g",MR),
}, {
/*  [50]  */	TNS("movmskps",XMMOX3),	TNSZ("sqrtps",XMMO,16),	TNSZ("rsqrtps",XMMO,16),TNSZ("rcpps",XMMO,16),
/*  [54]  */	TNSZ("andps",XMMO,16),	TNSZ("andnps",XMMO,16),	TNSZ("orps",XMMO,16),	TNSZ("xorps",XMMO,16),
/*  [58]  */	TNSZ("addps",XMMO,16),	TNSZ("mulps",XMMO,16),	TNSZ("cvtps2pd",XMMO,8),TNSZ("cvtdq2ps",XMMO,16),
/*  [5C]  */	TNSZ("subps",XMMO,16),	TNSZ("minps",XMMO,16),	TNSZ("divps",XMMO,16),	TNSZ("maxps",XMMO,16),
}, {
/*  [60]  */	TNSZ("punpcklbw",MMO,4),TNSZ("punpcklwd",MMO,4),TNSZ("punpckldq",MMO,4),TNSZ("packsswb",MMO,8),
/*  [64]  */	TNSZ("pcmpgtb",MMO,8),	TNSZ("pcmpgtw",MMO,8),	TNSZ("pcmpgtd",MMO,8),	TNSZ("packuswb",MMO,8),
/*  [68]  */	TNSZ("punpckhbw",MMO,8),TNSZ("punpckhwd",MMO,8),TNSZ("punpckhdq",MMO,8),TNSZ("packssdw",MMO,8),
/*  [6C]  */	TNSZ("INVALID",MMO,0),	TNSZ("INVALID",MMO,0),	TNSZ("movd",MMO,4),	TNSZ("movq",MMO,8),
}, {
/*  [70]  */	TNSZ("pshufw",MMOPM,8),	TNS("psrXXX",MR),	TNS("psrXXX",MR),	TNS("psrXXX",MR),
/*  [74]  */	TNSZ("pcmpeqb",MMO,8),	TNSZ("pcmpeqw",MMO,8),	TNSZ("pcmpeqd",MMO,8),	TNS("emms",NORM),
/*  [78]  */	TNSy("vmread",RM),	TNSy("vmwrite",MR),	INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		TNSZ("movd",MMOS,4),	TNSZ("movq",MMOS,8),
}, {
/*  [80]  */	TNS("jo",D),		TNS("jno",D),		TNS("jb",D),		TNS("jae",D),
/*  [84]  */	TNS("je",D),		TNS("jne",D),		TNS("jbe",D),		TNS("ja",D),
/*  [88]  */	TNS("js",D),		TNS("jns",D),		TNS("jp",D),		TNS("jnp",D),
/*  [8C]  */	TNS("jl",D),		TNS("jge",D),		TNS("jle",D),		TNS("jg",D),
}, {
/*  [90]  */	TNS("seto",Mb),		TNS("setno",Mb),	TNS("setb",Mb),		TNS("setae",Mb),
/*  [94]  */	TNS("sete",Mb),		TNS("setne",Mb),	TNS("setbe",Mb),	TNS("seta",Mb),
/*  [98]  */	TNS("sets",Mb),		TNS("setns",Mb),	TNS("setp",Mb),		TNS("setnp",Mb),
/*  [9C]  */	TNS("setl",Mb),		TNS("setge",Mb),	TNS("setle",Mb),	TNS("setg",Mb),
}, {
/*  [A0]  */	TSp("push",LSEG),	TSp("pop",LSEG),	TNS("cpuid",NORM),	TS("bt",RMw),
/*  [A4]  */	TS("shld",DSHIFT),	TS("shld",DSHIFTcl),	INVALID,		INVALID,
/*  [A8]  */	TSp("push",LSEG),	TSp("pop",LSEG),	TNS("rsm",NORM),	TS("bts",RMw),
/*  [AC]  */	TS("shrd",DSHIFT),	TS("shrd",DSHIFTcl),	IND(dis_op0FAE),	TS("imul",MRw),
}, {
/*  [B0]  */	TNS("cmpxchgb",RMw),	TS("cmpxchg",RMw),	TS("lss",MR),		TS("btr",RMw),
/*  [B4]  */	TS("lfs",MR),		TS("lgs",MR),		TS("movzb",MOVZ),	TNS("movzwl",MOVZ),
/*  [B8]  */	TNS("INVALID",MRw),	INVALID,		IND(dis_op0FBA),	TS("btc",RMw),
/*  [BC]  */	TS("bsf",MRw),		TS("bsr",MRw),		TS("movsb",MOVZ),	TNS("movswl",MOVZ),
}, {
/*  [C0]  */	TNS("xaddb",XADDB),	TS("xadd",RMw),		TNSZ("cmpps",XMMOPM,16),TNS("movnti",RM),
/*  [C4]  */	TNSZ("pinsrw",MMOPRM,2),TNS("pextrw",MMO3P), 	TNSZ("shufps",XMMOPM,16),IND(dis_op0FC7),
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [D0]  */	INVALID,		TNSZ("psrlw",MMO,8),	TNSZ("psrld",MMO,8),	TNSZ("psrlq",MMO,8),
/*  [D4]  */	TNSZ("paddq",MMO,8),	TNSZ("pmullw",MMO,8),	TNSZ("INVALID",MMO,0),	TNS("pmovmskb",MMOM3),
/*  [D8]  */	TNSZ("psubusb",MMO,8),	TNSZ("psubusw",MMO,8),	TNSZ("pminub",MMO,8),	TNSZ("pand",MMO,8),
/*  [DC]  */	TNSZ("paddusb",MMO,8),	TNSZ("paddusw",MMO,8),	TNSZ("pmaxub",MMO,8),	TNSZ("pandn",MMO,8),
}, {
/*  [E0]  */	TNSZ("pavgb",MMO,8),	TNSZ("psraw",MMO,8),	TNSZ("psrad",MMO,8),	TNSZ("pavgw",MMO,8),
/*  [E4]  */	TNSZ("pmulhuw",MMO,8),	TNSZ("pmulhw",MMO,8),	TNS("INVALID",XMMO),	TNSZ("movntq",MMOMS,8),
/*  [E8]  */	TNSZ("psubsb",MMO,8),	TNSZ("psubsw",MMO,8),	TNSZ("pminsw",MMO,8),	TNSZ("por",MMO,8),
/*  [EC]  */	TNSZ("paddsb",MMO,8),	TNSZ("paddsw",MMO,8),	TNSZ("pmaxsw",MMO,8),	TNSZ("pxor",MMO,8),
}, {
/*  [F0]  */	INVALID,		TNSZ("psllw",MMO,8),	TNSZ("pslld",MMO,8),	TNSZ("psllq",MMO,8),
/*  [F4]  */	TNSZ("pmuludq",MMO,8),	TNSZ("pmaddwd",MMO,8),	TNSZ("psadbw",MMO,8),	TNSZ("maskmovq",MMOIMPL,8),
/*  [F8]  */	TNSZ("psubb",MMO,8),	TNSZ("psubw",MMO,8),	TNSZ("psubd",MMO,8),	TNSZ("psubq",MMO,8),
/*  [FC]  */	TNSZ("paddb",MMO,8),	TNSZ("paddw",MMO,8),	TNSZ("paddd",MMO,8),	INVALID,
} };

const instable_t dis_opAVX0F[16][16] = {
{
/*  [00]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [08]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [0C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [10]  */	TNSZ("vmovups",VEX_MX,16),	TNSZ("vmovups",VEX_RM,16),TNSZ("vmovlps",VEX_RMrX,8),	TNSZ("vmovlps",VEX_RM,8),
/*  [14]  */	TNSZ("vunpcklps",VEX_RMrX,16),TNSZ("vunpckhps",VEX_RMrX,16),TNSZ("vmovhps",VEX_RMrX,8),TNSZ("vmovhps",VEX_RM,8),
/*  [18]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [1C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [20]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [24]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [28]  */	TNSZ("vmovaps",VEX_MX,16),	TNSZ("vmovaps",VEX_RX,16),INVALID,		TNSZ("vmovntps",VEX_RM,16),
/*  [2C]  */	INVALID,		INVALID,		TNSZ("vucomiss",VEX_MX,4),TNSZ("vcomiss",VEX_MX,4),
}, {
/*  [30]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [34]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [38]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [40]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [44]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [48]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [4C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [50]  */	TNS("vmovmskps",VEX_MR),	TNSZ("vsqrtps",VEX_MX,16),	TNSZ("vrsqrtps",VEX_MX,16),TNSZ("vrcpps",VEX_MX,16),
/*  [54]  */	TNSZ("vandps",VEX_RMrX,16),	TNSZ("vandnps",VEX_RMrX,16),	TNSZ("vorps",VEX_RMrX,16),	TNSZ("vxorps",VEX_RMrX,16),
/*  [58]  */	TNSZ("vaddps",VEX_RMrX,16),	TNSZ("vmulps",VEX_RMrX,16),	TNSZ("vcvtps2pd",VEX_MX,8),TNSZ("vcvtdq2ps",VEX_MX,16),
/*  [5C]  */	TNSZ("vsubps",VEX_RMrX,16),	TNSZ("vminps",VEX_RMrX,16),	TNSZ("vdivps",VEX_RMrX,16),	TNSZ("vmaxps",VEX_RMrX,16),
}, {
/*  [60]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [64]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [68]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [6C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [70]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [74]  */	INVALID,		INVALID,		INVALID,		TNS("vzeroupper", VEX_NONE),
/*  [78]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [7C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [80]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [84]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [88]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [8C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [90]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [94]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [98]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [9C]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [A0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [A8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [AC]  */	INVALID,		INVALID,		TNSZ("vldmxcsr",VEX_MO,2),		INVALID,
}, {
/*  [B0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [B8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [BC]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [C0]  */	INVALID,		INVALID,		TNSZ("vcmpps",VEX_RMRX,16),INVALID,
/*  [C4]  */	INVALID,		INVALID,	 	TNSZ("vshufps",VEX_RMRX,16),INVALID,
/*  [C8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [CC]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [D0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [D8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [DC]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [E0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E4]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [E8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [EC]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [F0]  */	INVALID,		INVALID,		TNSZvr("andn",VEX_RMrX,5),TNSZvr("bls",BLS,5),
/*  [F4]  */	INVALID,		TNSZvr("bzhi",VEX_VRMrX,5),INVALID,		TNSZvr("bextr",VEX_VRMrX,5),
/*  [F8]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [FC]  */	INVALID,		INVALID,		INVALID,		INVALID,
} };

/*
 *	Decode table for 0x80 opcodes
 */

const instable_t dis_op80[8] = {

/*  [0]  */	TNS("addb",IMlw),	TNS("orb",IMw),		TNS("adcb",IMlw),	TNS("sbbb",IMlw),
/*  [4]  */	TNS("andb",IMw),	TNS("subb",IMlw),	TNS("xorb",IMw),	TNS("cmpb",IMlw),
};


/*
 *	Decode table for 0x81 opcodes.
 */

const instable_t dis_op81[8] = {

/*  [0]  */	TS("add",IMlw),		TS("or",IMw),		TS("adc",IMlw),		TS("sbb",IMlw),
/*  [4]  */	TS("and",IMw),		TS("sub",IMlw),		TS("xor",IMw),		TS("cmp",IMlw),
};


/*
 *	Decode table for 0x82 opcodes.
 */

const instable_t dis_op82[8] = {

/*  [0]  */	TNSx("addb",IMlw),	TNSx("orb",IMlw),	TNSx("adcb",IMlw),	TNSx("sbbb",IMlw),
/*  [4]  */	TNSx("andb",IMlw),	TNSx("subb",IMlw),	TNSx("xorb",IMlw),	TNSx("cmpb",IMlw),
};
/*
 *	Decode table for 0x83 opcodes.
 */

const instable_t dis_op83[8] = {

/*  [0]  */	TS("add",IMlw),		TS("or",IMlw),		TS("adc",IMlw),		TS("sbb",IMlw),
/*  [4]  */	TS("and",IMlw),		TS("sub",IMlw),		TS("xor",IMlw),		TS("cmp",IMlw),
};

/*
 *	Decode table for 0xC0 opcodes.
 */

const instable_t dis_opC0[8] = {

/*  [0]  */	TNS("rolb",MvI),	TNS("rorb",MvI),	TNS("rclb",MvI),	TNS("rcrb",MvI),
/*  [4]  */	TNS("shlb",MvI),	TNS("shrb",MvI),	INVALID,		TNS("sarb",MvI),
};

/*
 *	Decode table for 0xD0 opcodes.
 */

const instable_t dis_opD0[8] = {

/*  [0]  */	TNS("rolb",Mv),		TNS("rorb",Mv),		TNS("rclb",Mv),		TNS("rcrb",Mv),
/*  [4]  */	TNS("shlb",Mv),		TNS("shrb",Mv),		TNS("salb",Mv),		TNS("sarb",Mv),
};

/*
 *	Decode table for 0xC1 opcodes.
 *	186 instruction set
 */

const instable_t dis_opC1[8] = {

/*  [0]  */	TS("rol",MvI),		TS("ror",MvI),		TS("rcl",MvI),		TS("rcr",MvI),
/*  [4]  */	TS("shl",MvI),		TS("shr",MvI),		TS("sal",MvI),		TS("sar",MvI),
};

/*
 *	Decode table for 0xD1 opcodes.
 */

const instable_t dis_opD1[8] = {

/*  [0]  */	TS("rol",Mv),		TS("ror",Mv),		TS("rcl",Mv),		TS("rcr",Mv),
/*  [4]  */	TS("shl",Mv),		TS("shr",Mv),		TS("sal",Mv),		TS("sar",Mv),
};


/*
 *	Decode table for 0xD2 opcodes.
 */

const instable_t dis_opD2[8] = {

/*  [0]  */	TNS("rolb",Mv),		TNS("rorb",Mv),		TNS("rclb",Mv),		TNS("rcrb",Mv),
/*  [4]  */	TNS("shlb",Mv),		TNS("shrb",Mv),		TNS("salb",Mv),		TNS("sarb",Mv),
};
/*
 *	Decode table for 0xD3 opcodes.
 */

const instable_t dis_opD3[8] = {

/*  [0]  */	TS("rol",Mv),		TS("ror",Mv),		TS("rcl",Mv),		TS("rcr",Mv),
/*  [4]  */	TS("shl",Mv),		TS("shr",Mv),		TS("salb",Mv),		TS("sar",Mv),
};


/*
 *	Decode table for 0xF6 opcodes.
 */

const instable_t dis_opF6[8] = {

/*  [0]  */	TNS("testb",IMw),	TNS("testb",IMw),	TNS("notb",Mw),		TNS("negb",Mw),
/*  [4]  */	TNS("mulb",MA),		TNS("imulb",MA),	TNS("divb",MA),		TNS("idivb",MA),
};


/*
 *	Decode table for 0xF7 opcodes.
 */

const instable_t dis_opF7[8] = {

/*  [0]  */	TS("test",IMw),		TS("test",IMw),		TS("not",Mw),		TS("neg",Mw),
/*  [4]  */	TS("mul",MA),		TS("imul",MA),		TS("div",MA),		TS("idiv",MA),
};


/*
 *	Decode table for 0xFE opcodes.
 */

const instable_t dis_opFE[8] = {

/*  [0]  */	TNS("incb",Mw),		TNS("decb",Mw),		INVALID,		INVALID,
/*  [4]  */	INVALID,		INVALID,		INVALID,		INVALID,
};
/*
 *	Decode table for 0xFF opcodes.
 */

const instable_t dis_opFF[8] = {

/*  [0]  */	TS("inc",Mw),		TS("dec",Mw),		TNSyp("call",INM),	TNS("lcall",INM),
/*  [4]  */	TNSy("jmp",INM),	TNS("ljmp",INM),	TSp("push",M),		INVALID,
};

/* for 287 instructions, which are a mess to decode */

const instable_t dis_opFP1n2[8][8] = {
{
/* bit pattern:	1101 1xxx MODxx xR/M */
/*  [0,0] */	TNS("fadds",M),		TNS("fmuls",M),		TNS("fcoms",M),		TNS("fcomps",M),
/*  [0,4] */	TNS("fsubs",M),		TNS("fsubrs",M),	TNS("fdivs",M),		TNS("fdivrs",M),
}, {
/*  [1,0]  */	TNS("flds",M),		INVALID,		TNS("fsts",M),		TNS("fstps",M),
/*  [1,4]  */	TNSZ("fldenv",M,28),	TNSZ("fldcw",M,2),	TNSZ("fnstenv",M,28),	TNSZ("fnstcw",M,2),
}, {
/*  [2,0]  */	TNS("fiaddl",M),	TNS("fimull",M),	TNS("ficoml",M),	TNS("ficompl",M),
/*  [2,4]  */	TNS("fisubl",M),	TNS("fisubrl",M),	TNS("fidivl",M),	TNS("fidivrl",M),
}, {
/*  [3,0]  */	TNS("fildl",M),		TNSZ("tisttpl",M,4),	TNS("fistl",M),		TNS("fistpl",M),
/*  [3,4]  */	INVALID,		TNSZ("fldt",M,10),	INVALID,		TNSZ("fstpt",M,10),
}, {
/*  [4,0]  */	TNSZ("faddl",M,8),	TNSZ("fmull",M,8),	TNSZ("fcoml",M,8),	TNSZ("fcompl",M,8),
/*  [4,1]  */	TNSZ("fsubl",M,8),	TNSZ("fsubrl",M,8),	TNSZ("fdivl",M,8),	TNSZ("fdivrl",M,8),
}, {
/*  [5,0]  */	TNSZ("fldl",M,8),	TNSZ("fisttpll",M,8),	TNSZ("fstl",M,8),	TNSZ("fstpl",M,8),
/*  [5,4]  */	TNSZ("frstor",M,108),	INVALID,		TNSZ("fnsave",M,108),	TNSZ("fnstsw",M,2),
}, {
/*  [6,0]  */	TNSZ("fiadd",M,2),	TNSZ("fimul",M,2),	TNSZ("ficom",M,2),	TNSZ("ficomp",M,2),
/*  [6,4]  */	TNSZ("fisub",M,2),	TNSZ("fisubr",M,2),	TNSZ("fidiv",M,2),	TNSZ("fidivr",M,2),
}, {
/*  [7,0]  */	TNSZ("fild",M,2),	TNSZ("fisttp",M,2),	TNSZ("fist",M,2),	TNSZ("fistp",M,2),
/*  [7,4]  */	TNSZ("fbld",M,10),	TNSZ("fildll",M,8),	TNSZ("fbstp",M,10),	TNSZ("fistpll",M,8),
} };

const instable_t dis_opFP3[8][8] = {
{
/* bit  pattern:	1101 1xxx 11xx xREG */
/*  [0,0]  */	TNS("fadd",FF),		TNS("fmul",FF),		TNS("fcom",F),		TNS("fcomp",F),
/*  [0,4]  */	TNS("fsub",FF),		TNS("fsubr",FF),	TNS("fdiv",FF),		TNS("fdivr",FF),
}, {
/*  [1,0]  */	TNS("fld",F),		TNS("fxch",F),		TNS("fnop",NORM),	TNS("fstp",F),
/*  [1,4]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [2,0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [2,4]  */	INVALID,		TNS("fucompp",NORM),	INVALID,		INVALID,
}, {
/*  [3,0]  */	INVALID,		INVALID,		INVALID,		INVALID,
/*  [3,4]  */	INVALID,		INVALID,		INVALID,		INVALID,
}, {
/*  [4,0]  */	TNS("fadd",FF),		TNS("fmul",FF),		TNS("fcom",F),		TNS("fcomp",F),
/*  [4,4]  */	TNS("fsub",FF),		TNS("fsubr",FF),	TNS("fdiv",FF),		TNS("fdivr",FF),
}, {
/*  [5,0]  */	TNS("ffree",F),		TNS("fxch",F),		TNS("fst",F),		TNS("fstp",F),
/*  [5,4]  */	TNS("fucom",F),		TNS("fucomp",F),	INVALID,		INVALID,
}, {
/*  [6,0]  */	TNS("faddp",FF),	TNS("fmulp",FF),	TNS("fcomp",F),		TNS("fcompp",NORM),
/*  [6,4]  */	TNS("fsubp",FF),	TNS("fsubrp",FF),	TNS("fdivp",FF),	TNS("fdivrp",FF),
}, {
/*  [7,0]  */	TNS("ffreep",F),		TNS("fxch",F),		TNS("fstp",F),		TNS("fstp",F),
/*  [7,4]  */	TNS("fnstsw",M),	TNS("fucomip",FFC),	TNS("fcomip",FFC),	INVALID,
} };

const instable_t dis_opFP4[4][8] = {
{
/* bit pattern:	1101 1001 111x xxxx */
/*  [0,0]  */	TNS("fchs",NORM),	TNS("fabs",NORM),	INVALID,		INVALID,
/*  [0,4]  */	TNS("ftst",NORM),	TNS("fxam",NORM),	TNS("ftstp",NORM),	INVALID,
}, {
/*  [1,0]  */	TNS("fld1",NORM),	TNS("fldl2t",NORM),	TNS("fldl2e",NORM),	TNS("fldpi",NORM),
/*  [1,4]  */	TNS("fldlg2",NORM),	TNS("fldln2",NORM),	TNS("fldz",NORM),	INVALID,
}, {
/*  [2,0]  */	TNS("f2xm1",NORM),	TNS("fyl2x",NORM),	TNS("fptan",NORM),	TNS("fpatan",NORM),
/*  [2,4]  */	TNS("fxtract",NORM),	TNS("fprem1",NORM),	TNS("fdecstp",NORM),	TNS("fincstp",NORM),
}, {
/*  [3,0]  */	TNS("fprem",NORM),	TNS("fyl2xp1",NORM),	TNS("fsqrt",NORM),	TNS("fsincos",NORM),
/*  [3,4]  */	TNS("frndint",NORM),	TNS("fscale",NORM),	TNS("fsin",NORM),	TNS("fcos",NORM),
} };

const instable_t dis_opFP5[8] = {
/* bit pattern:	1101 1011 111x xxxx */
/*  [0]  */	TNS("feni",NORM),	TNS("fdisi",NORM),	TNS("fnclex",NORM),	TNS("fninit",NORM),
/*  [4]  */	TNS("fsetpm",NORM),	TNS("frstpm",NORM),	INVALID,		INVALID,
};

const instable_t dis_opFP6[8] = {
/* bit pattern:	1101 1011 11yy yxxx */
/*  [00]  */	TNS("fcmov.nb",FF),	TNS("fcmov.ne",FF),	TNS("fcmov.nbe",FF),	TNS("fcmov.nu",FF),
/*  [04]  */	INVALID,		TNS("fucomi",F),	TNS("fcomi",F),		INVALID,
};

const instable_t dis_opFP7[8] = {
/* bit pattern:	1101 1010 11yy yxxx */
/*  [00]  */	TNS("fcmov.b",FF),	TNS("fcmov.e",FF),	TNS("fcmov.be",FF),	TNS("fcmov.u",FF),
/*  [04]  */	INVALID,		INVALID,		INVALID,		INVALID,
};

/*
 *	Main decode table for the op codes.  The first two nibbles
 *	will be used as an index into the table.  If there is a
 *	a need to further decode an instruction, the array to be
 *	referenced is indicated with the other two entries being
 *	empty.
 */

const instable_t dis_distable[16][16] = {
{
/* [0,0] */	TNS("addb",RMw),	TS("add",RMw),		TNS("addb",MRw),	TS("add",MRw),
/* [0,4] */	TNS("addb",IA),		TS("add",IA),		TSx("push",SEG),	TSx("pop",SEG),
/* [0,8] */	TNS("orb",RMw),		TS("or",RMw),		TNS("orb",MRw),		TS("or",MRw),
/* [0,C] */	TNS("orb",IA),		TS("or",IA),		TSx("push",SEG),	IND(dis_op0F),
}, {
/* [1,0] */	TNS("adcb",RMw),	TS("adc",RMw),		TNS("adcb",MRw),	TS("adc",MRw),
/* [1,4] */	TNS("adcb",IA),		TS("adc",IA),		TSx("push",SEG),	TSx("pop",SEG),
/* [1,8] */	TNS("sbbb",RMw),	TS("sbb",RMw),		TNS("sbbb",MRw),	TS("sbb",MRw),
/* [1,C] */	TNS("sbbb",IA),		TS("sbb",IA),		TSx("push",SEG),	TSx("pop",SEG),
}, {
/* [2,0] */	TNS("andb",RMw),	TS("and",RMw),		TNS("andb",MRw),	TS("and",MRw),
/* [2,4] */	TNS("andb",IA),		TS("and",IA),		TNSx("%es:",OVERRIDE),	TNSx("daa",NORM),
/* [2,8] */	TNS("subb",RMw),	TS("sub",RMw),		TNS("subb",MRw),	TS("sub",MRw),
/* [2,C] */	TNS("subb",IA),		TS("sub",IA),		TNS("%cs:",OVERRIDE),	TNSx("das",NORM),
}, {
/* [3,0] */	TNS("xorb",RMw),	TS("xor",RMw),		TNS("xorb",MRw),	TS("xor",MRw),
/* [3,4] */	TNS("xorb",IA),		TS("xor",IA),		TNSx("%ss:",OVERRIDE),	TNSx("aaa",NORM),
/* [3,8] */	TNS("cmpb",RMw),	TS("cmp",RMw),		TNS("cmpb",MRw),	TS("cmp",MRw),
/* [3,C] */	TNS("cmpb",IA),		TS("cmp",IA),		TNSx("%ds:",OVERRIDE),	TNSx("aas",NORM),
}, {
/* [4,0] */	TSx("inc",R),		TSx("inc",R),		TSx("inc",R),		TSx("inc",R),
/* [4,4] */	TSx("inc",R),		TSx("inc",R),		TSx("inc",R),		TSx("inc",R),
/* [4,8] */	TSx("dec",R),		TSx("dec",R),		TSx("dec",R),		TSx("dec",R),
/* [4,C] */	TSx("dec",R),		TSx("dec",R),		TSx("dec",R),		TSx("dec",R),
}, {
/* [5,0] */	TSp("push",R),		TSp("push",R),		TSp("push",R),		TSp("push",R),
/* [5,4] */	TSp("push",R),		TSp("push",R),		TSp("push",R),		TSp("push",R),
/* [5,8] */	TSp("pop",R),		TSp("pop",R),		TSp("pop",R),		TSp("pop",R),
/* [5,C] */	TSp("pop",R),		TSp("pop",R),		TSp("pop",R),		TSp("pop",R),
}, {
/* [6,0] */	TSZx("pusha",IMPLMEM,28),TSZx("popa",IMPLMEM,28), TSx("bound",MR),	TNS("arpl",RMw),
/* [6,4] */	TNS("%fs:",OVERRIDE),	TNS("%gs:",OVERRIDE),	TNS("data16",DM),	TNS("addr16",AM),
/* [6,8] */	TSp("push",I),		TS("imul",IMUL),	TSp("push",Ib),	TS("imul",IMUL),
/* [6,C] */	TNSZ("insb",IMPLMEM,1),	TSZ("ins",IMPLMEM,4),	TNSZ("outsb",IMPLMEM,1),TSZ("outs",IMPLMEM,4),
}, {
/* [7,0] */	TNSy("jo",BD),		TNSy("jno",BD),		TNSy("jb",BD),		TNSy("jae",BD),
/* [7,4] */	TNSy("je",BD),		TNSy("jne",BD),		TNSy("jbe",BD),		TNSy("ja",BD),
/* [7,8] */	TNSy("js",BD),		TNSy("jns",BD),		TNSy("jp",BD),		TNSy("jnp",BD),
/* [7,C] */	TNSy("jl",BD),		TNSy("jge",BD),		TNSy("jle",BD),		TNSy("jg",BD),
}, {
/* [8,0] */	IND(dis_op80),		IND(dis_op81),		INDx(dis_op82),		IND(dis_op83),
/* [8,4] */	TNS("testb",RMw),	TS("test",RMw),		TNS("xchgb",RMw),	TS("xchg",RMw),
/* [8,8] */	TNS("movb",RMw),	TS("mov",RMw),		TNS("movb",MRw),	TS("mov",MRw),
/* [8,C] */	TNS("movw",SM),		TS("lea",MR),		TNS("movw",MS),		TSp("pop",M),
}, {
/* [9,0] */	TNS("nop",NORM),	TS("xchg",RA),		TS("xchg",RA),		TS("xchg",RA),
/* [9,4] */	TS("xchg",RA),		TS("xchg",RA),		TS("xchg",RA),		TS("xchg",RA),
/* [9,8] */	TNS("cXtX",CBW),	TNS("cXtX",CWD),	TNSx("lcall",SO),	TNS("fwait",NORM),
/* [9,C] */	TSZy("pushf",IMPLMEM,4),TSZy("popf",IMPLMEM,4),	TNS("sahf",NORM),	TNS("lahf",NORM),
}, {
/* [A,0] */	TNS("movb",OA),		TS("mov",OA),		TNS("movb",AO),		TS("mov",AO),
/* [A,4] */	TNSZ("movsb",SD,1),	TS("movs",SD),		TNSZ("cmpsb",SD,1),	TS("cmps",SD),
/* [A,8] */	TNS("testb",IA),	TS("test",IA),		TNS("stosb",AD),	TS("stos",AD),
/* [A,C] */	TNS("lodsb",SA),	TS("lods",SA),		TNS("scasb",AD),	TS("scas",AD),
}, {
/* [B,0] */	TNS("movb",IR),		TNS("movb",IR),		TNS("movb",IR),		TNS("movb",IR),
/* [B,4] */	TNS("movb",IR),		TNS("movb",IR),		TNS("movb",IR),		TNS("movb",IR),
/* [B,8] */	TS("mov",IR),		TS("mov",IR),		TS("mov",IR),		TS("mov",IR),
/* [B,C] */	TS("mov",IR),		TS("mov",IR),		TS("mov",IR),		TS("mov",IR),
}, {
/* [C,0] */	IND(dis_opC0),		IND(dis_opC1), 		TNSyp("ret",RET),	TNSyp("ret",NORM),
/* [C,4] */	TNSx("les",MR),		TNSx("lds",MR),		TNS("movb",IMw),	TS("mov",IMw),
/* [C,8] */	TNSyp("enter",ENTER),	TNSyp("leave",NORM),	TNS("lret",RET),	TNS("lret",NORM),
/* [C,C] */	TNS("int",INT3),	TNS("int",INTx),	TNSx("into",NORM),	TNS("iret",NORM),
}, {
/* [D,0] */	IND(dis_opD0),		IND(dis_opD1),		IND(dis_opD2),		IND(dis_opD3),
/* [D,4] */	TNSx("aam",U),		TNSx("aad",U),		TNSx("falc",NORM),	TNSZ("xlat",IMPLMEM,1),

/* 287 instructions.  Note that although the indirect field		*/
/* indicates opFP1n2 for further decoding, this is not necessarily	*/
/* the case since the opFP arrays are not partitioned according to key1	*/
/* and key2.  opFP1n2 is given only to indicate that we haven't		*/
/* finished decoding the instruction.					*/
/* [D,8] */	IND(dis_opFP1n2),	IND(dis_opFP1n2),	IND(dis_opFP1n2),	IND(dis_opFP1n2),
/* [D,C] */	IND(dis_opFP1n2),	IND(dis_opFP1n2),	IND(dis_opFP1n2),	IND(dis_opFP1n2),
}, {
/* [E,0] */	TNSy("loopnz",BD),	TNSy("loopz",BD),	TNSy("loop",BD),	TNSy("jcxz",BD),
/* [E,4] */	TNS("inb",P),		TS("in",P),		TNS("outb",P),		TS("out",P),
/* [E,8] */	TNSyp("call",D),	TNSy("jmp",D),		TNSx("ljmp",SO),		TNSy("jmp",BD),
/* [E,C] */	TNS("inb",V),		TS("in",V),		TNS("outb",V),		TS("out",V),
}, {
/* [F,0] */	TNS("lock",LOCK),	TNS("icebp", NORM),	TNS("repnz",PREFIX),	TNS("repz",PREFIX),
/* [F,4] */	TNS("hlt",NORM),	TNS("cmc",NORM),	IND(dis_opF6),		IND(dis_opF7),
/* [F,8] */	TNS("clc",NORM),	TNS("stc",NORM),	TNS("cli",NORM),	TNS("sti",NORM),
/* [F,C] */	TNS("cld",NORM),	TNS("std",NORM),	IND(dis_opFE),		IND(dis_opFF),
} };

/* END CSTYLED */

/*
 * common functions to decode and disassemble an x86 or amd64 instruction
 */

/*
 * These are the individual fields of a REX prefix. Note that a REX
 * prefix with none of these set is still needed to:
 *	- use the MOVSXD (sign extend 32 to 64 bits) instruction
 *	- access the %sil, %dil, %bpl, %spl registers
 */
#define	REX_W 0x08	/* 64 bit operand size when set */
#define	REX_R 0x04	/* high order bit extension of ModRM reg field */
#define	REX_X 0x02	/* high order bit extension of SIB index field */
#define	REX_B 0x01	/* extends ModRM r_m, SIB base, or opcode reg */

/*
 * These are the individual fields of a VEX prefix.
 */
#define	VEX_R 0x08	/* REX.R in 1's complement form */
#define	VEX_X 0x04	/* REX.X in 1's complement form */
#define	VEX_B 0x02	/* REX.B in 1's complement form */
/* Vector Length, 0: scalar or 128-bit vector, 1: 256-bit vector */
#define	VEX_L 0x04
#define	VEX_W 0x08	/* opcode specific, use like REX.W */
#define	VEX_m 0x1F	/* VEX m-mmmm field */
#define	VEX_v 0x78	/* VEX register specifier */
#define	VEX_p 0x03	/* VEX pp field, opcode extension */

/* VEX m-mmmm field, only used by three bytes prefix */
#define	VEX_m_0F 0x01   /* implied 0F leading opcode byte */
#define	VEX_m_0F38 0x02 /* implied 0F 38 leading opcode byte */
#define	VEX_m_0F3A 0x03 /* implied 0F 3A leading opcode byte */

/* VEX pp field, providing equivalent functionality of a SIMD prefix */
#define	VEX_p_66 0x01
#define	VEX_p_F3 0x02
#define	VEX_p_F2 0x03

/*
 * Even in 64 bit mode, usually only 4 byte immediate operands are supported.
 */
static int isize[] = {1, 2, 4, 4};
static int isize64[] = {1, 2, 4, 8};

/*
 * Just a bunch of useful macros.
 */
#define	WBIT(x)	(x & 0x1)		/* to get w bit	*/
#define	REGNO(x) (x & 0x7)		/* to get 3 bit register */
#define	VBIT(x)	((x)>>1 & 0x1)		/* to get 'v' bit */
#define	OPSIZE(osize, wbit) ((wbit) ? isize[osize] : 1)
#define	OPSIZE64(osize, wbit) ((wbit) ? isize64[osize] : 1)

#define	REG_ONLY 3	/* mode to indicate a register operand (not memory) */

#define	BYTE_OPND	0	/* w-bit value indicating byte register */
#define	LONG_OPND	1	/* w-bit value indicating opnd_size register */
#define	MM_OPND		2	/* "value" used to indicate a mmx reg */
#define	XMM_OPND	3	/* "value" used to indicate a xmm reg */
#define	SEG_OPND	4	/* "value" used to indicate a segment reg */
#define	CONTROL_OPND	5	/* "value" used to indicate a control reg */
#define	DEBUG_OPND	6	/* "value" used to indicate a debug reg */
#define	TEST_OPND	7	/* "value" used to indicate a test reg */
#define	WORD_OPND	8	/* w-bit value indicating word size reg */
#define	YMM_OPND	9	/* "value" used to indicate a ymm reg */

/*
 * The AVX2 gather instructions are a bit of a mess. While there's a pattern,
 * there's not really a consistent scheme that we can use to know what the mode
 * is supposed to be for a given type. Various instructions, like VPGATHERDD,
 * always match the value of VEX_L. Other instructions like VPGATHERDQ, have
 * some registers match VEX_L, but the VSIB is always XMM.
 *
 * The simplest way to deal with this is to just define a table based on the
 * instruction opcodes, which are 0x90-0x93, so we subtract 0x90 to index into
 * them.
 *
 * We further have to subdivide this based on the value of VEX_W and the value
 * of VEX_L. The array is constructed to be indexed as:
 * 	[opcode - 0x90][VEX_W][VEX_L].
 */
/* w = 0, 0x90 */
typedef struct dis_gather_regs {
	uint_t dgr_arg0;	/* src reg */
	uint_t dgr_arg1;	/* vsib reg */
	uint_t dgr_arg2;	/* dst reg */
	char   *dgr_suffix;	/* suffix to append */
} dis_gather_regs_t;

static dis_gather_regs_t dis_vgather[4][2][2] = {
	{
		/* op 0x90, W.0 */
		{
			{ XMM_OPND, XMM_OPND, XMM_OPND, "d" },
			{ YMM_OPND, YMM_OPND, YMM_OPND, "d" }
		},
		/* op 0x90, W.1 */
		{
			{ XMM_OPND, XMM_OPND, XMM_OPND, "q" },
			{ YMM_OPND, XMM_OPND, YMM_OPND, "q" }
		}
	},
	{
		/* op 0x91, W.0 */
		{
			{ XMM_OPND, XMM_OPND, XMM_OPND, "d" },
			{ XMM_OPND, YMM_OPND, XMM_OPND, "d" },
		},
		/* op 0x91, W.1 */
		{
			{ XMM_OPND, XMM_OPND, XMM_OPND, "q" },
			{ YMM_OPND, YMM_OPND, YMM_OPND, "q" },
		}
	},
	{
		/* op 0x92, W.0 */
		{
			{ XMM_OPND, XMM_OPND, XMM_OPND, "s" },
			{ YMM_OPND, YMM_OPND, YMM_OPND, "s" }
		},
		/* op 0x92, W.1 */
		{
			{ XMM_OPND, XMM_OPND, XMM_OPND, "d" },
			{ YMM_OPND, XMM_OPND, YMM_OPND, "d" }
		}
	},
	{
		/* op 0x93, W.0 */
		{
			{ XMM_OPND, XMM_OPND, XMM_OPND, "s" },
			{ XMM_OPND, YMM_OPND, XMM_OPND, "s" }
		},
		/* op 0x93, W.1 */
		{
			{ XMM_OPND, XMM_OPND, XMM_OPND, "d" },
			{ YMM_OPND, YMM_OPND, YMM_OPND, "d" }
		}
	}
};

/*
 * Get the next byte and separate the op code into the high and low nibbles.
 */
static int
dtrace_get_opcode(dis86_t *x, uint_t *high, uint_t *low)
{
	int byte;

	/*
	 * x86 instructions have a maximum length of 15 bytes.  Bail out if
	 * we try to read more.
	 */
	if (x->d86_len >= 15)
		return (x->d86_error = 1);

	if (x->d86_error)
		return (1);
	byte = x->d86_get_byte(x->d86_data);
	if (byte < 0)
		return (x->d86_error = 1);
	x->d86_bytes[x->d86_len++] = byte;
	*low = byte & 0xf;		/* ----xxxx low 4 bits */
	*high = byte >> 4 & 0xf;	/* xxxx---- bits 7 to 4 */
	return (0);
}

/*
 * Get and decode an SIB (scaled index base) byte
 */
static void
dtrace_get_SIB(dis86_t *x, uint_t *ss, uint_t *index, uint_t *base)
{
	int byte;

	if (x->d86_error)
		return;

	byte = x->d86_get_byte(x->d86_data);
	if (byte < 0) {
		x->d86_error = 1;
		return;
	}
	x->d86_bytes[x->d86_len++] = byte;

	*base = byte & 0x7;
	*index = (byte >> 3) & 0x7;
	*ss = (byte >> 6) & 0x3;
}

/*
 * Get the byte following the op code and separate it into the
 * mode, register, and r/m fields.
 */
static void
dtrace_get_modrm(dis86_t *x, uint_t *mode, uint_t *reg, uint_t *r_m)
{
	if (x->d86_got_modrm == 0) {
		if (x->d86_rmindex == -1)
			x->d86_rmindex = x->d86_len;
		dtrace_get_SIB(x, mode, reg, r_m);
		x->d86_got_modrm = 1;
	}
}

/*
 * Adjust register selection based on any REX prefix bits present.
 */
/*ARGSUSED*/
static void
dtrace_rex_adjust(uint_t rex_prefix, uint_t mode, uint_t *reg, uint_t *r_m)
{
	if (reg != NULL && r_m == NULL) {
		if (rex_prefix & REX_B)
			*reg += 8;
	} else {
		if (reg != NULL && (REX_R & rex_prefix) != 0)
			*reg += 8;
		if (r_m != NULL && (REX_B & rex_prefix) != 0)
			*r_m += 8;
	}
}

/*
 * Adjust register selection based on any VEX prefix bits present.
 * Notes: VEX.R, VEX.X and VEX.B use the inverted form compared with REX prefix
 */
/*ARGSUSED*/
static void
dtrace_vex_adjust(uint_t vex_byte1, uint_t mode, uint_t *reg, uint_t *r_m)
{
	if (reg != NULL && r_m == NULL) {
		if (!(vex_byte1 & VEX_B))
			*reg += 8;
	} else {
		if (reg != NULL && ((VEX_R & vex_byte1) == 0))
			*reg += 8;
		if (r_m != NULL && ((VEX_B & vex_byte1) == 0))
			*r_m += 8;
	}
}

/*
 * Get an immediate operand of the given size, with sign extension.
 */
static void
dtrace_imm_opnd(dis86_t *x, int wbit, int size, int opindex)
{
	int i;
	int byte;
	int valsize;

	if (x->d86_numopnds < opindex + 1)
		x->d86_numopnds = opindex + 1;

	switch (wbit) {
	case BYTE_OPND:
		valsize = 1;
		break;
	case LONG_OPND:
		if (x->d86_opnd_size == SIZE16)
			valsize = 2;
		else if (x->d86_opnd_size == SIZE32)
			valsize = 4;
		else
			valsize = 8;
		break;
	case MM_OPND:
	case XMM_OPND:
	case YMM_OPND:
	case SEG_OPND:
	case CONTROL_OPND:
	case DEBUG_OPND:
	case TEST_OPND:
		valsize = size;
		break;
	case WORD_OPND:
		valsize = 2;
		break;
	}
	if (valsize < size)
		valsize = size;

	if (x->d86_error)
		return;
	x->d86_opnd[opindex].d86_value = 0;
	for (i = 0; i < size; ++i) {
		byte = x->d86_get_byte(x->d86_data);
		if (byte < 0) {
			x->d86_error = 1;
			return;
		}
		x->d86_bytes[x->d86_len++] = byte;
		x->d86_opnd[opindex].d86_value |= (uint64_t)byte << (i * 8);
	}
	/* Do sign extension */
	if (x->d86_bytes[x->d86_len - 1] & 0x80) {
		for (; i < sizeof (uint64_t); i++)
			x->d86_opnd[opindex].d86_value |=
			    (uint64_t)0xff << (i * 8);
	}
#ifdef DIS_TEXT
	x->d86_opnd[opindex].d86_mode = MODE_SIGNED;
	x->d86_opnd[opindex].d86_value_size = valsize;
	x->d86_imm_bytes += size;
#endif
}

/*
 * Get an ip relative operand of the given size, with sign extension.
 */
static void
dtrace_disp_opnd(dis86_t *x, int wbit, int size, int opindex)
{
	dtrace_imm_opnd(x, wbit, size, opindex);
#ifdef DIS_TEXT
	x->d86_opnd[opindex].d86_mode = MODE_IPREL;
#endif
}

/*
 * Check to see if there is a segment override prefix pending.
 * If so, print it in the current 'operand' location and set
 * the override flag back to false.
 */
/*ARGSUSED*/
static void
dtrace_check_override(dis86_t *x, int opindex)
{
#ifdef DIS_TEXT
	if (x->d86_seg_prefix) {
		(void) strlcat(x->d86_opnd[opindex].d86_prefix,
		    x->d86_seg_prefix, PFIXLEN);
	}
#endif
	x->d86_seg_prefix = NULL;
}


/*
 * Process a single instruction Register or Memory operand.
 *
 * mode = addressing mode from ModRM byte
 * r_m = r_m (or reg if mode == 3) field from ModRM byte
 * wbit = indicates which register (8bit, 16bit, ... MMX, etc.) set to use.
 * o = index of operand that we are processing (0, 1 or 2)
 *
 * the value of reg or r_m must have already been adjusted for any REX prefix.
 */
/*ARGSUSED*/
static void
dtrace_get_operand(dis86_t *x, uint_t mode, uint_t r_m, int wbit, int opindex)
{
	int have_SIB = 0;	/* flag presence of scale-index-byte */
	uint_t ss;		/* scale-factor from opcode */
	uint_t index;		/* index register number */
	uint_t base;		/* base register number */
	int dispsize;   	/* size of displacement in bytes */
#ifdef DIS_TEXT
	char *opnd = x->d86_opnd[opindex].d86_opnd;
#endif

	if (x->d86_numopnds < opindex + 1)
		x->d86_numopnds = opindex + 1;

	if (x->d86_error)
		return;

	/*
	 * first handle a simple register
	 */
	if (mode == REG_ONLY) {
#ifdef DIS_TEXT
		switch (wbit) {
		case MM_OPND:
			(void) strlcat(opnd, dis_MMREG[r_m], OPLEN);
			break;
		case XMM_OPND:
			(void) strlcat(opnd, dis_XMMREG[r_m], OPLEN);
			break;
		case YMM_OPND:
			(void) strlcat(opnd, dis_YMMREG[r_m], OPLEN);
			break;
		case SEG_OPND:
			(void) strlcat(opnd, dis_SEGREG[r_m], OPLEN);
			break;
		case CONTROL_OPND:
			(void) strlcat(opnd, dis_CONTROLREG[r_m], OPLEN);
			break;
		case DEBUG_OPND:
			(void) strlcat(opnd, dis_DEBUGREG[r_m], OPLEN);
			break;
		case TEST_OPND:
			(void) strlcat(opnd, dis_TESTREG[r_m], OPLEN);
			break;
		case BYTE_OPND:
			if (x->d86_rex_prefix == 0)
				(void) strlcat(opnd, dis_REG8[r_m], OPLEN);
			else
				(void) strlcat(opnd, dis_REG8_REX[r_m], OPLEN);
			break;
		case WORD_OPND:
			(void) strlcat(opnd, dis_REG16[r_m], OPLEN);
			break;
		case LONG_OPND:
			if (x->d86_opnd_size == SIZE16)
				(void) strlcat(opnd, dis_REG16[r_m], OPLEN);
			else if (x->d86_opnd_size == SIZE32)
				(void) strlcat(opnd, dis_REG32[r_m], OPLEN);
			else
				(void) strlcat(opnd, dis_REG64[r_m], OPLEN);
			break;
		}
#endif /* DIS_TEXT */
		return;
	}

	/*
	 * if symbolic representation, skip override prefix, if any
	 */
	dtrace_check_override(x, opindex);

	/*
	 * Handle 16 bit memory references first, since they decode
	 * the mode values more simply.
	 * mode 1 is r_m + 8 bit displacement
	 * mode 2 is r_m + 16 bit displacement
	 * mode 0 is just r_m, unless r_m is 6 which is 16 bit disp
	 */
	if (x->d86_addr_size == SIZE16) {
		if ((mode == 0 && r_m == 6) || mode == 2)
			dtrace_imm_opnd(x, WORD_OPND, 2, opindex);
		else if (mode == 1)
			dtrace_imm_opnd(x, BYTE_OPND, 1, opindex);
#ifdef DIS_TEXT
		if (mode == 0 && r_m == 6)
			x->d86_opnd[opindex].d86_mode = MODE_SIGNED;
		else if (mode == 0)
			x->d86_opnd[opindex].d86_mode = MODE_NONE;
		else
			x->d86_opnd[opindex].d86_mode = MODE_OFFSET;
		(void) strlcat(opnd, dis_addr16[mode][r_m], OPLEN);
#endif
		return;
	}

	/*
	 * 32 and 64 bit addressing modes are more complex since they
	 * can involve an SIB (scaled index and base) byte to decode.
	 */
	if (r_m == ESP_REGNO || r_m == ESP_REGNO + 8) {
		have_SIB = 1;
		dtrace_get_SIB(x, &ss, &index, &base);
		if (x->d86_error)
			return;
		if (base != 5 || mode != 0)
			if (x->d86_rex_prefix & REX_B)
				base += 8;
		if (x->d86_rex_prefix & REX_X)
			index += 8;
	} else {
		base = r_m;
	}

	/*
	 * Compute the displacement size and get its bytes
	 */
	dispsize = 0;

	if (mode == 1)
		dispsize = 1;
	else if (mode == 2)
		dispsize = 4;
	else if ((r_m & 7) == EBP_REGNO ||
	    (have_SIB && (base & 7) == EBP_REGNO))
		dispsize = 4;

	if (dispsize > 0) {
		dtrace_imm_opnd(x, dispsize == 4 ? LONG_OPND : BYTE_OPND,
		    dispsize, opindex);
		if (x->d86_error)
			return;
	}

#ifdef DIS_TEXT
	if (dispsize > 0)
		x->d86_opnd[opindex].d86_mode = MODE_OFFSET;

	if (have_SIB == 0) {
		if (x->d86_mode == SIZE32) {
			if (mode == 0)
				(void) strlcat(opnd, dis_addr32_mode0[r_m],
				    OPLEN);
			else
				(void) strlcat(opnd, dis_addr32_mode12[r_m],
				    OPLEN);
		} else {
			if (mode == 0) {
				(void) strlcat(opnd, dis_addr64_mode0[r_m],
				    OPLEN);
				if (r_m == 5) {
					x->d86_opnd[opindex].d86_mode =
					    MODE_RIPREL;
				}
			} else {
				(void) strlcat(opnd, dis_addr64_mode12[r_m],
				    OPLEN);
			}
		}
	} else {
		uint_t need_paren = 0;
		char **regs;
		char **bregs;
		const char *const *sf;
		if (x->d86_mode == SIZE32) /* NOTE this is not addr_size! */
			regs = (char **)dis_REG32;
		else
			regs = (char **)dis_REG64;

		if (x->d86_vsib != 0) {
			if (wbit == YMM_OPND) /* NOTE this is not addr_size! */
				bregs = (char **)dis_YMMREG;
			else
				bregs = (char **)dis_XMMREG;
			sf = dis_vscale_factor;
		} else {
			bregs = regs;
			sf = dis_scale_factor;
		}

		/*
		 * print the base (if any)
		 */
		if (base == EBP_REGNO && mode == 0) {
			if (index != ESP_REGNO || x->d86_vsib != 0) {
				(void) strlcat(opnd, "(", OPLEN);
				need_paren = 1;
			}
		} else {
			(void) strlcat(opnd, "(", OPLEN);
			(void) strlcat(opnd, regs[base], OPLEN);
			need_paren = 1;
		}

		/*
		 * print the index (if any)
		 */
		if (index != ESP_REGNO || x->d86_vsib) {
			(void) strlcat(opnd, ",", OPLEN);
			(void) strlcat(opnd, bregs[index], OPLEN);
			(void) strlcat(opnd, sf[ss], OPLEN);
		} else
			if (need_paren)
				(void) strlcat(opnd, ")", OPLEN);
	}
#endif
}

/*
 * Operand sequence for standard instruction involving one register
 * and one register/memory operand.
 * wbit indicates a byte(0) or opnd_size(1) operation
 * vbit indicates direction (0 for "opcode r,r_m") or (1 for "opcode r_m, r")
 */
#define	STANDARD_MODRM(x, mode, reg, r_m, rex_prefix, wbit, vbit)  {	\
		dtrace_get_modrm(x, &mode, &reg, &r_m);			\
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);	\
		dtrace_get_operand(x, mode, r_m, wbit, vbit);		\
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 1 - vbit);	\
}

/*
 * Similar to above, but allows for the two operands to be of different
 * classes (ie. wbit).
 *	wbit is for the r_m operand
 *	w2 is for the reg operand
 */
#define	MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, w2, vbit)	{	\
		dtrace_get_modrm(x, &mode, &reg, &r_m);			\
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);	\
		dtrace_get_operand(x, mode, r_m, wbit, vbit);		\
		dtrace_get_operand(x, REG_ONLY, reg, w2, 1 - vbit);	\
}

/*
 * Similar, but for 2 operands plus an immediate.
 * vbit indicates direction
 * 	0 for "opcode imm, r, r_m" or
 *	1 for "opcode imm, r_m, r"
 */
#define	THREEOPERAND(x, mode, reg, r_m, rex_prefix, wbit, w2, immsize, vbit) { \
		dtrace_get_modrm(x, &mode, &reg, &r_m);			\
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);	\
		dtrace_get_operand(x, mode, r_m, wbit, 2-vbit);		\
		dtrace_get_operand(x, REG_ONLY, reg, w2, 1+vbit);	\
		dtrace_imm_opnd(x, wbit, immsize, 0);			\
}

/*
 * Similar, but for 2 operands plus two immediates.
 */
#define	FOUROPERAND(x, mode, reg, r_m, rex_prefix, wbit, w2, immsize) { \
		dtrace_get_modrm(x, &mode, &reg, &r_m);			\
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);	\
		dtrace_get_operand(x, mode, r_m, wbit, 2);		\
		dtrace_get_operand(x, REG_ONLY, reg, w2, 3);		\
		dtrace_imm_opnd(x, wbit, immsize, 1);			\
		dtrace_imm_opnd(x, wbit, immsize, 0);			\
}

/*
 * 1 operands plus two immediates.
 */
#define	ONEOPERAND_TWOIMM(x, mode, reg, r_m, rex_prefix, wbit, immsize) { \
		dtrace_get_modrm(x, &mode, &reg, &r_m);			\
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);	\
		dtrace_get_operand(x, mode, r_m, wbit, 2);		\
		dtrace_imm_opnd(x, wbit, immsize, 1);			\
		dtrace_imm_opnd(x, wbit, immsize, 0);			\
}

/*
 * Dissassemble a single x86 or amd64 instruction.
 *
 * Mode determines the default operating mode (SIZE16, SIZE32 or SIZE64)
 * for interpreting instructions.
 *
 * returns non-zero for bad opcode
 */
int
dtrace_disx86(dis86_t *x, uint_t cpu_mode)
{
	instable_t *dp;		/* decode table being used */
#ifdef DIS_TEXT
	uint_t i;
#endif
#ifdef DIS_MEM
	uint_t nomem = 0;
#define	NOMEM	(nomem = 1)
#else
#define	NOMEM	/* nothing */
#endif
	uint_t opnd_size;	/* SIZE16, SIZE32 or SIZE64 */
	uint_t addr_size;	/* SIZE16, SIZE32 or SIZE64 */
	uint_t wbit;		/* opcode wbit, 0 is 8 bit, !0 for opnd_size */
	uint_t w2;		/* wbit value for second operand */
	uint_t vbit;
	uint_t mode = 0;	/* mode value from ModRM byte */
	uint_t reg;		/* reg value from ModRM byte */
	uint_t r_m;		/* r_m value from ModRM byte */

	uint_t opcode1;		/* high nibble of 1st byte */
	uint_t opcode2;		/* low nibble of 1st byte */
	uint_t opcode3;		/* extra opcode bits usually from ModRM byte */
	uint_t opcode4;		/* high nibble of 2nd byte */
	uint_t opcode5;		/* low nibble of 2nd byte */
	uint_t opcode6;		/* high nibble of 3rd byte */
	uint_t opcode7;		/* low nibble of 3rd byte */
	uint_t opcode_bytes = 1;

	/*
	 * legacy prefixes come in 5 flavors, you should have only one of each
	 */
	uint_t	opnd_size_prefix = 0;
	uint_t	addr_size_prefix = 0;
	uint_t	segment_prefix = 0;
	uint_t	lock_prefix = 0;
	uint_t	rep_prefix = 0;
	uint_t	rex_prefix = 0;	/* amd64 register extension prefix */

	/*
	 * Intel VEX instruction encoding prefix and fields
	 */

	/* 0xC4 means 3 bytes prefix, 0xC5 means 2 bytes prefix */
	uint_t vex_prefix = 0;

	/*
	 * VEX prefix byte 1, includes vex.r, vex.x and vex.b
	 * (for 3 bytes prefix)
	 */
	uint_t vex_byte1 = 0;

	/*
	 * For 32-bit mode, it should prefetch the next byte to
	 * distinguish between AVX and les/lds
	 */
	uint_t vex_prefetch = 0;

	uint_t vex_m = 0;
	uint_t vex_v = 0;
	uint_t vex_p = 0;
	uint_t vex_R = 1;
	uint_t vex_X = 1;
	uint_t vex_B = 1;
	uint_t vex_W = 0;
	uint_t vex_L;
	dis_gather_regs_t *vreg;

#ifdef	DIS_TEXT
	/* Instruction name for BLS* family of instructions */
	char *blsinstr;
#endif

	size_t	off;

	instable_t dp_mmx;

	x->d86_len = 0;
	x->d86_rmindex = -1;
	x->d86_error = 0;
#ifdef DIS_TEXT
	x->d86_numopnds = 0;
	x->d86_seg_prefix = NULL;
	x->d86_mnem[0] = 0;
	for (i = 0; i < 4; ++i) {
		x->d86_opnd[i].d86_opnd[0] = 0;
		x->d86_opnd[i].d86_prefix[0] = 0;
		x->d86_opnd[i].d86_value_size = 0;
		x->d86_opnd[i].d86_value = 0;
		x->d86_opnd[i].d86_mode = MODE_NONE;
	}
#endif
	x->d86_rex_prefix = 0;
	x->d86_got_modrm = 0;
	x->d86_memsize = 0;
	x->d86_vsib = 0;

	if (cpu_mode == SIZE16) {
		opnd_size = SIZE16;
		addr_size = SIZE16;
	} else if (cpu_mode == SIZE32) {
		opnd_size = SIZE32;
		addr_size = SIZE32;
	} else {
		opnd_size = SIZE32;
		addr_size = SIZE64;
	}

	/*
	 * Get one opcode byte and check for zero padding that follows
	 * jump tables.
	 */
	if (dtrace_get_opcode(x, &opcode1, &opcode2) != 0)
		goto error;

	if (opcode1 == 0 && opcode2 == 0 &&
	    x->d86_check_func != NULL && x->d86_check_func(x->d86_data)) {
#ifdef DIS_TEXT
		(void) strncpy(x->d86_mnem, ".byte\t0", OPLEN);
#endif
		goto done;
	}

	/*
	 * Gather up legacy x86 prefix bytes.
	 */
	for (;;) {
		uint_t *which_prefix = NULL;

		dp = (instable_t *)&dis_distable[opcode1][opcode2];

		switch (dp->it_adrmode) {
		case PREFIX:
			which_prefix = &rep_prefix;
			break;
		case LOCK:
			which_prefix = &lock_prefix;
			break;
		case OVERRIDE:
			which_prefix = &segment_prefix;
#ifdef DIS_TEXT
			x->d86_seg_prefix = (char *)dp->it_name;
#endif
			if (dp->it_invalid64 && cpu_mode == SIZE64)
				goto error;
			break;
		case AM:
			which_prefix = &addr_size_prefix;
			break;
		case DM:
			which_prefix = &opnd_size_prefix;
			break;
		}
		if (which_prefix == NULL)
			break;
		*which_prefix = (opcode1 << 4) | opcode2;
		if (dtrace_get_opcode(x, &opcode1, &opcode2) != 0)
			goto error;
	}

	/*
	 * Handle amd64 mode PREFIX values.
	 * Some of the segment prefixes are no-ops. (only FS/GS actually work)
	 * We might have a REX prefix (opcodes 0x40-0x4f)
	 */
	if (cpu_mode == SIZE64) {
		if (segment_prefix != 0x64 && segment_prefix != 0x65)
			segment_prefix = 0;

		if (opcode1 == 0x4) {
			rex_prefix = (opcode1 << 4) | opcode2;
			if (dtrace_get_opcode(x, &opcode1, &opcode2) != 0)
				goto error;
			dp = (instable_t *)&dis_distable[opcode1][opcode2];
		} else if (opcode1 == 0xC &&
		    (opcode2 == 0x4 || opcode2 == 0x5)) {
			/* AVX instructions */
			vex_prefix = (opcode1 << 4) | opcode2;
			x->d86_rex_prefix = 0x40;
		}
	} else if (opcode1 == 0xC && (opcode2 == 0x4 || opcode2 == 0x5)) {
		/* LDS, LES or AVX */
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		vex_prefetch = 1;

		if (mode == REG_ONLY) {
			/* AVX */
			vex_prefix = (opcode1 << 4) | opcode2;
			x->d86_rex_prefix = 0x40;
			opcode3 = (((mode << 3) | reg)>>1) & 0x0F;
			opcode4 = ((reg << 3) | r_m) & 0x0F;
		}
	}

	if (vex_prefix == VEX_2bytes) {
		if (!vex_prefetch) {
			if (dtrace_get_opcode(x, &opcode3, &opcode4) != 0)
				goto error;
		}
		vex_R = ((opcode3 & VEX_R) & 0x0F) >> 3;
		vex_L = ((opcode4 & VEX_L) & 0x0F) >> 2;
		vex_v = (((opcode3 << 4) | opcode4) & VEX_v) >> 3;
		vex_p = opcode4 & VEX_p;
		/*
		 * The vex.x and vex.b bits are not defined in two bytes
		 * mode vex prefix, their default values are 1
		 */
		vex_byte1 = (opcode3 & VEX_R) | VEX_X | VEX_B;

		if (vex_R == 0)
			x->d86_rex_prefix |= REX_R;

		if (dtrace_get_opcode(x, &opcode1, &opcode2) != 0)
			goto error;

		switch (vex_p) {
			case VEX_p_66:
				dp = (instable_t *)
				    &dis_opAVX660F[(opcode1 << 4) | opcode2];
				break;
			case VEX_p_F3:
				dp = (instable_t *)
				    &dis_opAVXF30F[(opcode1 << 4) | opcode2];
				break;
			case VEX_p_F2:
				dp = (instable_t *)
				    &dis_opAVXF20F [(opcode1 << 4) | opcode2];
				break;
			default:
				dp = (instable_t *)
				    &dis_opAVX0F[opcode1][opcode2];

		}

	} else if (vex_prefix == VEX_3bytes) {
		if (!vex_prefetch) {
			if (dtrace_get_opcode(x, &opcode3, &opcode4) != 0)
				goto error;
		}
		vex_R = (opcode3 & VEX_R) >> 3;
		vex_X = (opcode3 & VEX_X) >> 2;
		vex_B = (opcode3 & VEX_B) >> 1;
		vex_m = (((opcode3 << 4) | opcode4) & VEX_m);
		vex_byte1 = opcode3 & (VEX_R | VEX_X | VEX_B);

		if (vex_R == 0)
			x->d86_rex_prefix |= REX_R;
		if (vex_X == 0)
			x->d86_rex_prefix |= REX_X;
		if (vex_B == 0)
			x->d86_rex_prefix |= REX_B;

		if (dtrace_get_opcode(x, &opcode5, &opcode6) != 0)
			goto error;
		vex_W = (opcode5 & VEX_W) >> 3;
		vex_L = (opcode6 & VEX_L) >> 2;
		vex_v = (((opcode5 << 4) | opcode6) & VEX_v) >> 3;
		vex_p = opcode6 & VEX_p;

		if (vex_W)
			x->d86_rex_prefix |= REX_W;

		/* Only these three vex_m values valid; others are reserved */
		if ((vex_m != VEX_m_0F) && (vex_m != VEX_m_0F38) &&
		    (vex_m != VEX_m_0F3A))
			goto error;

		if (dtrace_get_opcode(x, &opcode1, &opcode2) != 0)
			goto error;

		switch (vex_p) {
			case VEX_p_66:
				if (vex_m == VEX_m_0F) {
					dp = (instable_t *)
					    &dis_opAVX660F
					    [(opcode1 << 4) | opcode2];
				} else if (vex_m == VEX_m_0F38) {
					dp = (instable_t *)
					    &dis_opAVX660F38
					    [(opcode1 << 4) | opcode2];
				} else if (vex_m == VEX_m_0F3A) {
					dp = (instable_t *)
					    &dis_opAVX660F3A
					    [(opcode1 << 4) | opcode2];
				} else {
					goto error;
				}
				break;
			case VEX_p_F3:
				if (vex_m == VEX_m_0F) {
					dp = (instable_t *)
					    &dis_opAVXF30F
					    [(opcode1 << 4) | opcode2];
				} else if (vex_m == VEX_m_0F38) {
					dp = (instable_t *)
					    &dis_opAVXF30F38
					    [(opcode1 << 4) | opcode2];
				} else {
					goto error;
				}
				break;
			case VEX_p_F2:
				if (vex_m == VEX_m_0F) {
					dp = (instable_t *)
					    &dis_opAVXF20F
					    [(opcode1 << 4) | opcode2];
				} else if (vex_m == VEX_m_0F3A) {
					dp = (instable_t *)
					    &dis_opAVXF20F3A
					    [(opcode1 << 4) | opcode2];
				} else if (vex_m == VEX_m_0F38) {
					dp = (instable_t *)
					    &dis_opAVXF20F38
					    [(opcode1 << 4) | opcode2];
				} else {
					goto error;
				}
				break;
			default:
				dp = (instable_t *)
				    &dis_opAVX0F[opcode1][opcode2];

		}
	}
	if (vex_prefix) {
		if (dp->it_vexwoxmm) {
			wbit = LONG_OPND;
		} else {
			if (vex_L)
				wbit = YMM_OPND;
			else
				wbit = XMM_OPND;
		}
	}

	/*
	 * Deal with selection of operand and address size now.
	 * Note that the REX.W bit being set causes opnd_size_prefix to be
	 * ignored.
	 */
	if (cpu_mode == SIZE64) {
		if ((rex_prefix & REX_W) || vex_W)
			opnd_size = SIZE64;
		else if (opnd_size_prefix)
			opnd_size = SIZE16;

		if (addr_size_prefix)
			addr_size = SIZE32;
	} else if (cpu_mode == SIZE32) {
		if (opnd_size_prefix)
			opnd_size = SIZE16;
		if (addr_size_prefix)
			addr_size = SIZE16;
	} else {
		if (opnd_size_prefix)
			opnd_size = SIZE32;
		if (addr_size_prefix)
			addr_size = SIZE32;
	}
	/*
	 * The pause instruction - a repz'd nop.  This doesn't fit
	 * with any of the other prefix goop added for SSE, so we'll
	 * special-case it here.
	 */
	if (rep_prefix == 0xf3 && opcode1 == 0x9 && opcode2 == 0x0) {
		rep_prefix = 0;
		dp = (instable_t *)&dis_opPause;
	}

	/*
	 * Some 386 instructions have 2 bytes of opcode before the mod_r/m
	 * byte so we may need to perform a table indirection.
	 */
	if (dp->it_indirect == (instable_t *)dis_op0F) {
		if (dtrace_get_opcode(x, &opcode4, &opcode5) != 0)
			goto error;
		opcode_bytes = 2;
		if (opcode4 == 0x7 && opcode5 >= 0x1 && opcode5 <= 0x3) {
			uint_t	subcode;

			if (dtrace_get_opcode(x, &opcode6, &opcode7) != 0)
				goto error;
			opcode_bytes = 3;
			subcode = ((opcode6 & 0x3) << 1) |
			    ((opcode7 & 0x8) >> 3);
			dp = (instable_t *)&dis_op0F7123[opcode5][subcode];
		} else if ((opcode4 == 0xc) && (opcode5 >= 0x8)) {
			dp = (instable_t *)&dis_op0FC8[0];
		} else if ((opcode4 == 0x3) && (opcode5 == 0xA)) {
			opcode_bytes = 3;
			if (dtrace_get_opcode(x, &opcode6, &opcode7) != 0)
				goto error;
			if (opnd_size == SIZE16)
				opnd_size = SIZE32;

			dp = (instable_t *)&dis_op0F3A[(opcode6<<4)|opcode7];
#ifdef DIS_TEXT
			if (strcmp(dp->it_name, "INVALID") == 0)
				goto error;
#endif
			switch (dp->it_adrmode) {
				case XMMP:
					break;
				case XMMP_66r:
				case XMMPRM_66r:
				case XMM3PM_66r:
					if (opnd_size_prefix == 0) {
						goto error;
					}
					break;
				case XMMP_66o:
					if (opnd_size_prefix == 0) {
						/* SSSE3 MMX instructions */
						dp_mmx = *dp;
						dp = &dp_mmx;
						dp->it_adrmode = MMOPM_66o;
#ifdef	DIS_MEM
						dp->it_size = 8;
#endif
					}
					break;
				default:
					goto error;
			}
		} else if ((opcode4 == 0x3) && (opcode5 == 0x8)) {
			opcode_bytes = 3;
			if (dtrace_get_opcode(x, &opcode6, &opcode7) != 0)
				goto error;
			dp = (instable_t *)&dis_op0F38[(opcode6<<4)|opcode7];

			/*
			 * Both crc32 and movbe have the same 3rd opcode
			 * byte of either 0xF0 or 0xF1, so we use another
			 * indirection to distinguish between the two.
			 */
			if (dp->it_indirect == (instable_t *)dis_op0F38F0 ||
			    dp->it_indirect == (instable_t *)dis_op0F38F1) {

				dp = dp->it_indirect;
				if (rep_prefix != 0xF2) {
					/* It is movbe */
					dp++;
				}
			}

			/*
			 * The adx family of instructions (adcx and adox)
			 * continue the classic Intel tradition of abusing
			 * arbitrary prefixes without actually meaning the
			 * prefix bit. Therefore, if we find either the
			 * opnd_size_prefix or rep_prefix we end up zeroing it
			 * out after making our determination so as to ensure
			 * that we don't get confused and accidentally print
			 * repz prefixes and the like on these instructions.
			 *
			 * In addition, these instructions are actually much
			 * closer to AVX instructions in semantics. Importantly,
			 * they always default to having 32-bit operands.
			 * However, if the CPU is in 64-bit mode, then and only
			 * then, does it use REX.w promotes things to 64-bits
			 * and REX.r allows 64-bit mode to use register r8-r15.
			 */
			if (dp->it_indirect == (instable_t *)dis_op0F38F6) {
				dp = dp->it_indirect;
				if (opnd_size_prefix == 0 &&
				    rep_prefix == 0xf3) {
					/* It is adox */
					dp++;
				} else if (opnd_size_prefix != 0x66 &&
				    rep_prefix != 0) {
					/* It isn't adcx */
					goto error;
				}
				opnd_size_prefix = 0;
				rep_prefix = 0;
				opnd_size = SIZE32;
				if (rex_prefix & REX_W)
					opnd_size = SIZE64;
			}

#ifdef DIS_TEXT
			if (strcmp(dp->it_name, "INVALID") == 0)
				goto error;
#endif
			switch (dp->it_adrmode) {
				case ADX:
				case XMM:
					break;
				case RM_66r:
				case XMM_66r:
				case XMMM_66r:
					if (opnd_size_prefix == 0) {
						goto error;
					}
					break;
				case XMM_66o:
					if (opnd_size_prefix == 0) {
						/* SSSE3 MMX instructions */
						dp_mmx = *dp;
						dp = &dp_mmx;
						dp->it_adrmode = MM;
#ifdef	DIS_MEM
						dp->it_size = 8;
#endif
					}
					break;
				case CRC32:
					if (rep_prefix != 0xF2) {
						goto error;
					}
					rep_prefix = 0;
					break;
				case MOVBE:
					if (rep_prefix != 0x0) {
						goto error;
					}
					break;
				default:
					goto error;
			}
		} else {
			dp = (instable_t *)&dis_op0F[opcode4][opcode5];
		}
	}

	/*
	 * If still not at a TERM decode entry, then a ModRM byte
	 * exists and its fields further decode the instruction.
	 */
	x->d86_got_modrm = 0;
	if (dp->it_indirect != TERM) {
		dtrace_get_modrm(x, &mode, &opcode3, &r_m);
		if (x->d86_error)
			goto error;
		reg = opcode3;

		/*
		 * decode 287 instructions (D8-DF) from opcodeN
		 */
		if (opcode1 == 0xD && opcode2 >= 0x8) {
			if (opcode2 == 0xB && mode == 0x3 && opcode3 == 4)
				dp = (instable_t *)&dis_opFP5[r_m];
			else if (opcode2 == 0xA && mode == 0x3 && opcode3 < 4)
				dp = (instable_t *)&dis_opFP7[opcode3];
			else if (opcode2 == 0xB && mode == 0x3)
				dp = (instable_t *)&dis_opFP6[opcode3];
			else if (opcode2 == 0x9 && mode == 0x3 && opcode3 >= 4)
				dp = (instable_t *)&dis_opFP4[opcode3 - 4][r_m];
			else if (mode == 0x3)
				dp = (instable_t *)
				    &dis_opFP3[opcode2 - 8][opcode3];
			else
				dp = (instable_t *)
				    &dis_opFP1n2[opcode2 - 8][opcode3];
		} else {
			dp = (instable_t *)dp->it_indirect + opcode3;
		}
	}

	/*
	 * In amd64 bit mode, ARPL opcode is changed to MOVSXD
	 * (sign extend 32bit to 64 bit)
	 */
	if ((vex_prefix == 0) && cpu_mode == SIZE64 &&
	    opcode1 == 0x6 && opcode2 == 0x3)
		dp = (instable_t *)&dis_opMOVSLD;

	/*
	 * at this point we should have a correct (or invalid) opcode
	 */
	if (cpu_mode == SIZE64 && dp->it_invalid64 ||
	    cpu_mode != SIZE64 && dp->it_invalid32)
		goto error;
	if (dp->it_indirect != TERM)
		goto error;

	/*
	 * Deal with MMX/SSE opcodes which are changed by prefixes. Note, we do
	 * need to include UNKNOWN below, as we may have instructions that
	 * actually have a prefix, but don't exist in any other form.
	 */
	switch (dp->it_adrmode) {
	case UNKNOWN:
	case MMO:
	case MMOIMPL:
	case MMO3P:
	case MMOM3:
	case MMOMS:
	case MMOPM:
	case MMOPRM:
	case MMOS:
	case XMMO:
	case XMMOM:
	case XMMOMS:
	case XMMOPM:
	case XMMOS:
	case XMMOMX:
	case XMMOX3:
	case XMMOXMM:
		/*
		 * This is horrible.  Some SIMD instructions take the
		 * form 0x0F 0x?? ..., which is easily decoded using the
		 * existing tables.  Other SIMD instructions use various
		 * prefix bytes to overload existing instructions.  For
		 * Example, addps is F0, 58, whereas addss is F3 (repz),
		 * F0, 58.  Presumably someone got a raise for this.
		 *
		 * If we see one of the instructions which can be
		 * modified in this way (if we've got one of the SIMDO*
		 * address modes), we'll check to see if the last prefix
		 * was a repz.  If it was, we strip the prefix from the
		 * mnemonic, and we indirect using the dis_opSIMDrepz
		 * table.
		 */

		/*
		 * Calculate our offset in dis_op0F
		 */
		if ((uintptr_t)dp - (uintptr_t)dis_op0F > sizeof (dis_op0F))
			goto error;

		off = ((uintptr_t)dp - (uintptr_t)dis_op0F) /
		    sizeof (instable_t);

		/*
		 * Rewrite if this instruction used one of the magic prefixes.
		 */
		if (rep_prefix) {
			if (rep_prefix == 0xf2)
				dp = (instable_t *)&dis_opSIMDrepnz[off];
			else
				dp = (instable_t *)&dis_opSIMDrepz[off];
			rep_prefix = 0;
		} else if (opnd_size_prefix) {
			dp = (instable_t *)&dis_opSIMDdata16[off];
			opnd_size_prefix = 0;
			if (opnd_size == SIZE16)
				opnd_size = SIZE32;
		}
		break;

	case MG9:
		/*
		 * More horribleness: the group 9 (0xF0 0xC7) instructions are
		 * allowed an optional prefix of 0x66 or 0xF3.  This is similar
		 * to the SIMD business described above, but with a different
		 * addressing mode (and an indirect table), so we deal with it
		 * separately (if similarly).
		 *
		 * Intel further complicated this with the release of Ivy Bridge
		 * where they overloaded these instructions based on the ModR/M
		 * bytes. The VMX instructions have a mode of 0 since they are
		 * memory instructions but rdrand instructions have a mode of
		 * 0b11 (REG_ONLY) because they only operate on registers. While
		 * there are different prefix formats, for now it is sufficient
		 * to use a single different table.
		 */

		/*
		 * Calculate our offset in dis_op0FC7 (the group 9 table)
		 */
		if ((uintptr_t)dp - (uintptr_t)dis_op0FC7 > sizeof (dis_op0FC7))
			goto error;

		off = ((uintptr_t)dp - (uintptr_t)dis_op0FC7) /
		    sizeof (instable_t);

		/*
		 * If we have a mode of 0b11 then we have to rewrite this.
		 */
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		if (mode == REG_ONLY) {
			dp = (instable_t *)&dis_op0FC7m3[off];
			break;
		}

		/*
		 * Rewrite if this instruction used one of the magic prefixes.
		 */
		if (rep_prefix) {
			if (rep_prefix == 0xf3)
				dp = (instable_t *)&dis_opF30FC7[off];
			else
				goto error;
			rep_prefix = 0;
		} else if (opnd_size_prefix) {
			dp = (instable_t *)&dis_op660FC7[off];
			opnd_size_prefix = 0;
			if (opnd_size == SIZE16)
				opnd_size = SIZE32;
		}
		break;


	case MMOSH:
		/*
		 * As with the "normal" SIMD instructions, the MMX
		 * shuffle instructions are overloaded.  These
		 * instructions, however, are special in that they use
		 * an extra byte, and thus an extra table.  As of this
		 * writing, they only use the opnd_size prefix.
		 */

		/*
		 * Calculate our offset in dis_op0F7123
		 */
		if ((uintptr_t)dp - (uintptr_t)dis_op0F7123 >
		    sizeof (dis_op0F7123))
			goto error;

		if (opnd_size_prefix) {
			off = ((uintptr_t)dp - (uintptr_t)dis_op0F7123) /
			    sizeof (instable_t);
			dp = (instable_t *)&dis_opSIMD7123[off];
			opnd_size_prefix = 0;
			if (opnd_size == SIZE16)
				opnd_size = SIZE32;
		}
		break;
	case MRw:
		if (rep_prefix) {
			if (rep_prefix == 0xf3) {

				/*
				 * Calculate our offset in dis_op0F
				 */
				if ((uintptr_t)dp - (uintptr_t)dis_op0F
				    > sizeof (dis_op0F))
					goto error;

				off = ((uintptr_t)dp - (uintptr_t)dis_op0F) /
				    sizeof (instable_t);

				dp = (instable_t *)&dis_opSIMDrepz[off];
				rep_prefix = 0;
			} else {
				goto error;
			}
		}
		break;
	}

	/*
	 * In 64 bit mode, some opcodes automatically use opnd_size == SIZE64.
	 */
	if (cpu_mode == SIZE64)
		if (dp->it_always64 || (opnd_size == SIZE32 && dp->it_stackop))
			opnd_size = SIZE64;

#ifdef DIS_TEXT
	/*
	 * At this point most instructions can format the opcode mnemonic
	 * including the prefixes.
	 */
	if (lock_prefix)
		(void) strlcat(x->d86_mnem, "lock ", OPLEN);

	if (rep_prefix == 0xf2)
		(void) strlcat(x->d86_mnem, "repnz ", OPLEN);
	else if (rep_prefix == 0xf3)
		(void) strlcat(x->d86_mnem, "repz ", OPLEN);

	if (cpu_mode == SIZE64 && addr_size_prefix)
		(void) strlcat(x->d86_mnem, "addr32 ", OPLEN);

	if (dp->it_adrmode != CBW &&
	    dp->it_adrmode != CWD &&
	    dp->it_adrmode != XMMSFNC) {
		if (strcmp(dp->it_name, "INVALID") == 0)
			goto error;
		(void) strlcat(x->d86_mnem, dp->it_name, OPLEN);
		if (dp->it_avxsuf && dp->it_suffix) {
			(void) strlcat(x->d86_mnem, vex_W != 0 ? "q" : "d",
			    OPLEN);
		} else if (dp->it_suffix) {
			char *types[] = {"", "w", "l", "q"};
			if (opcode_bytes == 2 && opcode4 == 4) {
				/* It's a cmovx.yy. Replace the suffix x */
				for (i = 5; i < OPLEN; i++) {
					if (x->d86_mnem[i] == '.')
						break;
				}
				x->d86_mnem[i - 1] = *types[opnd_size];
			} else if ((opnd_size == 2) && (opcode_bytes == 3) &&
			    ((opcode6 == 1 && opcode7 == 6) ||
			    (opcode6 == 2 && opcode7 == 2))) {
				/*
				 * To handle PINSRD and PEXTRD
				 */
				(void) strlcat(x->d86_mnem, "d", OPLEN);
			} else {
				(void) strlcat(x->d86_mnem, types[opnd_size],
				    OPLEN);
			}
		}
	}
#endif

	/*
	 * Process operands based on the addressing modes.
	 */
	x->d86_mode = cpu_mode;
	/*
	 * In vex mode the rex_prefix has no meaning
	 */
	if (!vex_prefix)
		x->d86_rex_prefix = rex_prefix;
	x->d86_opnd_size = opnd_size;
	x->d86_addr_size = addr_size;
	vbit = 0;		/* initialize for mem/reg -> reg */
	switch (dp->it_adrmode) {
		/*
		 * amd64 instruction to sign extend 32 bit reg/mem operands
		 * into 64 bit register values
		 */
	case MOVSXZ:
#ifdef DIS_TEXT
		if (rex_prefix == 0)
			(void) strncpy(x->d86_mnem, "movzld", OPLEN);
#endif
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
		x->d86_opnd_size = SIZE64;
		dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 1);
		x->d86_opnd_size = opnd_size = SIZE32;
		wbit = LONG_OPND;
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;

		/*
		 * movsbl movsbw movsbq (0x0FBE) or movswl movswq (0x0FBF)
		 * movzbl movzbw movzbq (0x0FB6) or movzwl movzwq (0x0FB7)
		 * wbit lives in 2nd byte, note that operands
		 * are different sized
		 */
	case MOVZ:
		if (rex_prefix & REX_W) {
			/* target register size = 64 bit */
			x->d86_mnem[5] = 'q';
		}
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
		dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 1);
		x->d86_opnd_size = opnd_size = SIZE16;
		wbit = WBIT(opcode5);
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;
	case CRC32:
		opnd_size = SIZE32;
		if (rex_prefix & REX_W)
			opnd_size = SIZE64;
		x->d86_opnd_size = opnd_size;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
		dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 1);
		wbit = WBIT(opcode7);
		if (opnd_size_prefix)
			x->d86_opnd_size = opnd_size = SIZE16;
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;
	case MOVBE:
		opnd_size = SIZE32;
		if (rex_prefix & REX_W)
			opnd_size = SIZE64;
		x->d86_opnd_size = opnd_size;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
		wbit = WBIT(opcode7);
		if (opnd_size_prefix)
			x->d86_opnd_size = opnd_size = SIZE16;
		if (wbit) {
			/* reg -> mem */
			dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 0);
			dtrace_get_operand(x, mode, r_m, wbit, 1);
		} else {
			/* mem -> reg */
			dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 1);
			dtrace_get_operand(x, mode, r_m, wbit, 0);
		}
		break;

	/*
	 * imul instruction, with either 8-bit or longer immediate
	 * opcode 0x6B for byte, sign-extended displacement, 0x69 for word(s)
	 */
	case IMUL:
		wbit = LONG_OPND;
		THREEOPERAND(x, mode, reg, r_m, rex_prefix, wbit, LONG_OPND,
		    OPSIZE(opnd_size, opcode2 == 0x9), 1);
		break;

	/* memory or register operand to register, with 'w' bit	*/
	case MRw:
	case ADX:
		wbit = WBIT(opcode2);
		STANDARD_MODRM(x, mode, reg, r_m, rex_prefix, wbit, 0);
		break;

	/* register to memory or register operand, with 'w' bit	*/
	/* arpl happens to fit here also because it is odd */
	case RMw:
		if (opcode_bytes == 2)
			wbit = WBIT(opcode5);
		else
			wbit = WBIT(opcode2);
		STANDARD_MODRM(x, mode, reg, r_m, rex_prefix, wbit, 1);
		break;

	/* xaddb instruction */
	case XADDB:
		wbit = 0;
		STANDARD_MODRM(x, mode, reg, r_m, rex_prefix, wbit, 1);
		break;

	/* MMX register to memory or register operand		*/
	case MMS:
	case MMOS:
#ifdef DIS_TEXT
		wbit = strcmp(dp->it_name, "movd") ? MM_OPND : LONG_OPND;
#else
		wbit = LONG_OPND;
#endif
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, MM_OPND, 1);
		break;

	/* MMX register to memory */
	case MMOMS:
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		if (mode == REG_ONLY)
			goto error;
		wbit = MM_OPND;
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, MM_OPND, 1);
		break;

	/* Double shift. Has immediate operand specifying the shift. */
	case DSHIFT:
		wbit = LONG_OPND;
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 2);
		dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 1);
		dtrace_imm_opnd(x, wbit, 1, 0);
		break;

	/*
	 * Double shift. With no immediate operand, specifies using %cl.
	 */
	case DSHIFTcl:
		wbit = LONG_OPND;
		STANDARD_MODRM(x, mode, reg, r_m, rex_prefix, wbit, 1);
		break;

	/* immediate to memory or register operand */
	case IMlw:
		wbit = WBIT(opcode2);
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 1);
		/*
		 * Have long immediate for opcode 0x81, but not 0x80 nor 0x83
		 */
		dtrace_imm_opnd(x, wbit, OPSIZE(opnd_size, opcode2 == 1), 0);
		break;

	/* immediate to memory or register operand with the	*/
	/* 'w' bit present					*/
	case IMw:
		wbit = WBIT(opcode2);
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 1);
		dtrace_imm_opnd(x, wbit, OPSIZE(opnd_size, wbit), 0);
		break;

	/* immediate to register with register in low 3 bits	*/
	/* of op code						*/
	case IR:
		/* w-bit here (with regs) is bit 3 */
		wbit = opcode2 >>3 & 0x1;
		reg = REGNO(opcode2);
		dtrace_rex_adjust(rex_prefix, mode, &reg, NULL);
		mode = REG_ONLY;
		r_m = reg;
		dtrace_get_operand(x, mode, r_m, wbit, 1);
		dtrace_imm_opnd(x, wbit, OPSIZE64(opnd_size, wbit), 0);
		break;

	/* MMX immediate shift of register */
	case MMSH:
	case MMOSH:
		wbit = MM_OPND;
		goto mm_shift;	/* in next case */

	/* SIMD immediate shift of register */
	case XMMSH:
		wbit = XMM_OPND;
mm_shift:
		reg = REGNO(opcode7);
		dtrace_rex_adjust(rex_prefix, mode, &reg, NULL);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);
		dtrace_imm_opnd(x, wbit, 1, 0);
		NOMEM;
		break;

	/* accumulator to memory operand */
	case AO:
		vbit = 1;
		/*FALLTHROUGH*/

	/* memory operand to accumulator */
	case OA:
		wbit = WBIT(opcode2);
		dtrace_get_operand(x, REG_ONLY, EAX_REGNO, wbit, 1 - vbit);
		dtrace_imm_opnd(x, wbit, OPSIZE64(addr_size, LONG_OPND), vbit);
#ifdef DIS_TEXT
		x->d86_opnd[vbit].d86_mode = MODE_OFFSET;
#endif
		break;


	/* segment register to memory or register operand */
	case SM:
		vbit = 1;
		/*FALLTHROUGH*/

	/* memory or register operand to segment register */
	case MS:
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, LONG_OPND, vbit);
		dtrace_get_operand(x, REG_ONLY, reg, SEG_OPND, 1 - vbit);
		break;

	/*
	 * rotate or shift instructions, which may shift by 1 or
	 * consult the cl register, depending on the 'v' bit
	 */
	case Mv:
		vbit = VBIT(opcode2);
		wbit = WBIT(opcode2);
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 1);
#ifdef DIS_TEXT
		if (vbit) {
			(void) strlcat(x->d86_opnd[0].d86_opnd, "%cl", OPLEN);
		} else {
			x->d86_opnd[0].d86_mode = MODE_SIGNED;
			x->d86_opnd[0].d86_value_size = 1;
			x->d86_opnd[0].d86_value = 1;
		}
#endif
		break;
	/*
	 * immediate rotate or shift instructions
	 */
	case MvI:
		wbit = WBIT(opcode2);
normal_imm_mem:
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 1);
		dtrace_imm_opnd(x, wbit, 1, 0);
		break;

	/* bit test instructions */
	case MIb:
		wbit = LONG_OPND;
		goto normal_imm_mem;

	/* single memory or register operand with 'w' bit present */
	case Mw:
		wbit = WBIT(opcode2);
just_mem:
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;

	case SWAPGS_RDTSCP:
		if (cpu_mode == SIZE64 && mode == 3 && r_m == 0) {
#ifdef DIS_TEXT
			(void) strncpy(x->d86_mnem, "swapgs", OPLEN);
#endif
			NOMEM;
			break;
		} else if (mode == 3 && r_m == 1) {
#ifdef DIS_TEXT
			(void) strncpy(x->d86_mnem, "rdtscp", OPLEN);
#endif
			NOMEM;
			break;
		}

		/*FALLTHROUGH*/

	/* prefetch instruction - memory operand, but no memory acess */
	case PREF:
		NOMEM;
		/*FALLTHROUGH*/

	/* single memory or register operand */
	case M:
	case MG9:
		wbit = LONG_OPND;
		goto just_mem;

	/* single memory or register byte operand */
	case Mb:
		wbit = BYTE_OPND;
		goto just_mem;

	case VMx:
		if (mode == 3) {
#ifdef DIS_TEXT
			char *vminstr;

			switch (r_m) {
			case 1:
				vminstr = "vmcall";
				break;
			case 2:
				vminstr = "vmlaunch";
				break;
			case 3:
				vminstr = "vmresume";
				break;
			case 4:
				vminstr = "vmxoff";
				break;
			default:
				goto error;
			}

			(void) strncpy(x->d86_mnem, vminstr, OPLEN);
#else
			if (r_m < 1 || r_m > 4)
				goto error;
#endif

			NOMEM;
			break;
		}
		/*FALLTHROUGH*/
	case SVM:
		if (mode == 3) {
#ifdef DIS_TEXT
			char *vinstr;

			switch (r_m) {
			case 0:
				vinstr = "vmrun";
				break;
			case 1:
				vinstr = "vmmcall";
				break;
			case 2:
				vinstr = "vmload";
				break;
			case 3:
				vinstr = "vmsave";
				break;
			case 4:
				vinstr = "stgi";
				break;
			case 5:
				vinstr = "clgi";
				break;
			case 6:
				vinstr = "skinit";
				break;
			case 7:
				vinstr = "invlpga";
				break;
			}

			(void) strncpy(x->d86_mnem, vinstr, OPLEN);
#endif
			NOMEM;
			break;
		}
		/*FALLTHROUGH*/
	case MONITOR_MWAIT:
		if (mode == 3) {
			if (r_m == 0) {
#ifdef DIS_TEXT
				(void) strncpy(x->d86_mnem, "monitor", OPLEN);
#endif
				NOMEM;
				break;
			} else if (r_m == 1) {
#ifdef DIS_TEXT
				(void) strncpy(x->d86_mnem, "mwait", OPLEN);
#endif
				NOMEM;
				break;
			} else if (r_m == 2) {
#ifdef DIS_TEXT
				(void) strncpy(x->d86_mnem, "clac", OPLEN);
#endif
				NOMEM;
				break;
			} else if (r_m == 3) {
#ifdef DIS_TEXT
				(void) strncpy(x->d86_mnem, "stac", OPLEN);
#endif
				NOMEM;
				break;
			} else {
				goto error;
			}
		}
		/*FALLTHROUGH*/
	case XGETBV_XSETBV:
		if (mode == 3) {
			if (r_m == 0) {
#ifdef DIS_TEXT
				(void) strncpy(x->d86_mnem, "xgetbv", OPLEN);
#endif
				NOMEM;
				break;
			} else if (r_m == 1) {
#ifdef DIS_TEXT
				(void) strncpy(x->d86_mnem, "xsetbv", OPLEN);
#endif
				NOMEM;
				break;
			} else {
				goto error;
			}

		}
		/*FALLTHROUGH*/
	case MO:
		/* Similar to M, but only memory (no direct registers) */
		wbit = LONG_OPND;
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		if (mode == 3)
			goto error;
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;

	/* move special register to register or reverse if vbit */
	case SREG:
		switch (opcode5) {

		case 2:
			vbit = 1;
			/*FALLTHROUGH*/
		case 0:
			wbit = CONTROL_OPND;
			break;

		case 3:
			vbit = 1;
			/*FALLTHROUGH*/
		case 1:
			wbit = DEBUG_OPND;
			break;

		case 6:
			vbit = 1;
			/*FALLTHROUGH*/
		case 4:
			wbit = TEST_OPND;
			break;

		}
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, vbit);
		dtrace_get_operand(x, REG_ONLY, r_m, LONG_OPND, 1 - vbit);
		NOMEM;
		break;

	/*
	 * single register operand with register in the low 3
	 * bits of op code
	 */
	case R:
		if (opcode_bytes == 2)
			reg = REGNO(opcode5);
		else
			reg = REGNO(opcode2);
		dtrace_rex_adjust(rex_prefix, mode, &reg, NULL);
		dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 0);
		NOMEM;
		break;

	/*
	 * register to accumulator with register in the low 3
	 * bits of op code, xchg instructions
	 */
	case RA:
		NOMEM;
		reg = REGNO(opcode2);
		dtrace_rex_adjust(rex_prefix, mode, &reg, NULL);
		dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 0);
		dtrace_get_operand(x, REG_ONLY, EAX_REGNO, LONG_OPND, 1);
		break;

	/*
	 * single segment register operand, with register in
	 * bits 3-4 of op code byte
	 */
	case SEG:
		NOMEM;
		reg = (x->d86_bytes[x->d86_len - 1] >> 3) & 0x3;
		dtrace_get_operand(x, REG_ONLY, reg, SEG_OPND, 0);
		break;

	/*
	 * single segment register operand, with register in
	 * bits 3-5 of op code
	 */
	case LSEG:
		NOMEM;
		/* long seg reg from opcode */
		reg = (x->d86_bytes[x->d86_len - 1] >> 3) & 0x7;
		dtrace_get_operand(x, REG_ONLY, reg, SEG_OPND, 0);
		break;

	/* memory or register operand to register */
	case MR:
		if (vex_prefetch)
			x->d86_got_modrm = 1;
		wbit = LONG_OPND;
		STANDARD_MODRM(x, mode, reg, r_m, rex_prefix, wbit, 0);
		break;

	case RM:
	case RM_66r:
		wbit = LONG_OPND;
		STANDARD_MODRM(x, mode, reg, r_m, rex_prefix, wbit, 1);
		break;

	/* MMX/SIMD-Int memory or mm reg to mm reg		*/
	case MM:
	case MMO:
#ifdef DIS_TEXT
		wbit = strcmp(dp->it_name, "movd") ? MM_OPND : LONG_OPND;
#else
		wbit = LONG_OPND;
#endif
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, MM_OPND, 0);
		break;

	case MMOIMPL:
#ifdef DIS_TEXT
		wbit = strcmp(dp->it_name, "movd") ? MM_OPND : LONG_OPND;
#else
		wbit = LONG_OPND;
#endif
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		if (mode != REG_ONLY)
			goto error;

		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		dtrace_get_operand(x, REG_ONLY, reg, MM_OPND, 1);
		mode = 0;	/* change for memory access size... */
		break;

	/* MMX/SIMD-Int and SIMD-FP predicated mm reg to r32 */
	case MMO3P:
		wbit = MM_OPND;
		goto xmm3p;
	case XMM3P:
		wbit = XMM_OPND;
xmm3p:
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		if (mode != REG_ONLY)
			goto error;

		THREEOPERAND(x, mode, reg, r_m, rex_prefix, wbit, LONG_OPND, 1,
		    1);
		NOMEM;
		break;

	case XMM3PM_66r:
		THREEOPERAND(x, mode, reg, r_m, rex_prefix, LONG_OPND, XMM_OPND,
		    1, 0);
		break;

	/* MMX/SIMD-Int predicated r32/mem to mm reg */
	case MMOPRM:
		wbit = LONG_OPND;
		w2 = MM_OPND;
		goto xmmprm;
	case XMMPRM:
	case XMMPRM_66r:
		wbit = LONG_OPND;
		w2 = XMM_OPND;
xmmprm:
		THREEOPERAND(x, mode, reg, r_m, rex_prefix, wbit, w2, 1, 1);
		break;

	/* MMX/SIMD-Int predicated mm/mem to mm reg */
	case MMOPM:
	case MMOPM_66o:
		wbit = w2 = MM_OPND;
		goto xmmprm;

	/* MMX/SIMD-Int mm reg to r32 */
	case MMOM3:
		NOMEM;
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		if (mode != REG_ONLY)
			goto error;
		wbit = MM_OPND;
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, LONG_OPND, 0);
		break;

	/* SIMD memory or xmm reg operand to xmm reg		*/
	case XMM:
	case XMM_66o:
	case XMM_66r:
	case XMMO:
	case XMMXIMPL:
		wbit = XMM_OPND;
		STANDARD_MODRM(x, mode, reg, r_m, rex_prefix, wbit, 0);

		if (dp->it_adrmode == XMMXIMPL && mode != REG_ONLY)
			goto error;

#ifdef DIS_TEXT
		/*
		 * movlps and movhlps share opcodes.  They differ in the
		 * addressing modes allowed for their operands.
		 * movhps and movlhps behave similarly.
		 */
		if (mode == REG_ONLY) {
			if (strcmp(dp->it_name, "movlps") == 0)
				(void) strncpy(x->d86_mnem, "movhlps", OPLEN);
			else if (strcmp(dp->it_name, "movhps") == 0)
				(void) strncpy(x->d86_mnem, "movlhps", OPLEN);
		}
#endif
		if (dp->it_adrmode == XMMXIMPL)
			mode = 0;	/* change for memory access size... */
		break;

	/* SIMD xmm reg to memory or xmm reg */
	case XMMS:
	case XMMOS:
	case XMMMS:
	case XMMOMS:
		dtrace_get_modrm(x, &mode, &reg, &r_m);
#ifdef DIS_TEXT
		if ((strcmp(dp->it_name, "movlps") == 0 ||
		    strcmp(dp->it_name, "movhps") == 0 ||
		    strcmp(dp->it_name, "movntps") == 0) &&
		    mode == REG_ONLY)
			goto error;
#endif
		wbit = XMM_OPND;
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, XMM_OPND, 1);
		break;

	/* SIMD memory to xmm reg */
	case XMMM:
	case XMMM_66r:
	case XMMOM:
		wbit = XMM_OPND;
		dtrace_get_modrm(x, &mode, &reg, &r_m);
#ifdef DIS_TEXT
		if (mode == REG_ONLY) {
			if (strcmp(dp->it_name, "movhps") == 0)
				(void) strncpy(x->d86_mnem, "movlhps", OPLEN);
			else
				goto error;
		}
#endif
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, XMM_OPND, 0);
		break;

	/* SIMD memory or r32 to xmm reg			*/
	case XMM3MX:
		wbit = LONG_OPND;
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, XMM_OPND, 0);
		break;

	case XMM3MXS:
		wbit = LONG_OPND;
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, XMM_OPND, 1);
		break;

	/* SIMD memory or mm reg to xmm reg			*/
	case XMMOMX:
	/* SIMD mm to xmm */
	case XMMMX:
		wbit = MM_OPND;
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, XMM_OPND, 0);
		break;

	/* SIMD memory or xmm reg to mm reg			*/
	case XMMXMM:
	case XMMOXMM:
	case XMMXM:
		wbit = XMM_OPND;
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, MM_OPND, 0);
		break;


	/* SIMD memory or xmm reg to r32			*/
	case XMMXM3:
		wbit = XMM_OPND;
		MIXED_MM(x, mode, reg, r_m, rex_prefix, wbit, LONG_OPND, 0);
		break;

	/* SIMD xmm to r32					*/
	case XMMX3:
	case XMMOX3:
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		if (mode != REG_ONLY)
			goto error;
		dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
		dtrace_get_operand(x, mode, r_m, XMM_OPND, 0);
		dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, 1);
		NOMEM;
		break;

	/* SIMD predicated memory or xmm reg with/to xmm reg */
	case XMMP:
	case XMMP_66r:
	case XMMP_66o:
	case XMMOPM:
		wbit = XMM_OPND;
		THREEOPERAND(x, mode, reg, r_m, rex_prefix, wbit, XMM_OPND, 1,
		    1);

#ifdef DIS_TEXT
		/*
		 * cmpps and cmpss vary their instruction name based
		 * on the value of imm8.  Other XMMP instructions,
		 * such as shufps, require explicit specification of
		 * the predicate.
		 */
		if (dp->it_name[0] == 'c' &&
		    dp->it_name[1] == 'm' &&
		    dp->it_name[2] == 'p' &&
		    strlen(dp->it_name) == 5) {
			uchar_t pred = x->d86_opnd[0].d86_value & 0xff;

			if (pred >= (sizeof (dis_PREDSUFFIX) / sizeof (char *)))
				goto error;

			(void) strncpy(x->d86_mnem, "cmp", OPLEN);
			(void) strlcat(x->d86_mnem, dis_PREDSUFFIX[pred],
			    OPLEN);
			(void) strlcat(x->d86_mnem,
			    dp->it_name + strlen(dp->it_name) - 2,
			    OPLEN);
			x->d86_opnd[0] = x->d86_opnd[1];
			x->d86_opnd[1] = x->d86_opnd[2];
			x->d86_numopnds = 2;
		}
#endif
		break;

	case XMMX2I:
		FOUROPERAND(x, mode, reg, r_m, rex_prefix, XMM_OPND, XMM_OPND,
		    1);
		NOMEM;
		break;

	case XMM2I:
		ONEOPERAND_TWOIMM(x, mode, reg, r_m, rex_prefix, XMM_OPND, 1);
		NOMEM;
		break;

	/* immediate operand to accumulator */
	case IA:
		wbit = WBIT(opcode2);
		dtrace_get_operand(x, REG_ONLY, EAX_REGNO, wbit, 1);
		dtrace_imm_opnd(x, wbit, OPSIZE(opnd_size, wbit), 0);
		NOMEM;
		break;

	/* memory or register operand to accumulator */
	case MA:
		wbit = WBIT(opcode2);
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;

	/* si register to di register used to reference memory		*/
	case SD:
#ifdef DIS_TEXT
		dtrace_check_override(x, 0);
		x->d86_numopnds = 2;
		if (addr_size == SIZE64) {
			(void) strlcat(x->d86_opnd[0].d86_opnd, "(%rsi)",
			    OPLEN);
			(void) strlcat(x->d86_opnd[1].d86_opnd, "(%rdi)",
			    OPLEN);
		} else if (addr_size == SIZE32) {
			(void) strlcat(x->d86_opnd[0].d86_opnd, "(%esi)",
			    OPLEN);
			(void) strlcat(x->d86_opnd[1].d86_opnd, "(%edi)",
			    OPLEN);
		} else {
			(void) strlcat(x->d86_opnd[0].d86_opnd, "(%si)",
			    OPLEN);
			(void) strlcat(x->d86_opnd[1].d86_opnd, "(%di)",
			    OPLEN);
		}
#endif
		wbit = LONG_OPND;
		break;

	/* accumulator to di register				*/
	case AD:
		wbit = WBIT(opcode2);
#ifdef DIS_TEXT
		dtrace_check_override(x, 1);
		x->d86_numopnds = 2;
		dtrace_get_operand(x, REG_ONLY, EAX_REGNO, wbit, 0);
		if (addr_size == SIZE64)
			(void) strlcat(x->d86_opnd[1].d86_opnd, "(%rdi)",
			    OPLEN);
		else if (addr_size == SIZE32)
			(void) strlcat(x->d86_opnd[1].d86_opnd, "(%edi)",
			    OPLEN);
		else
			(void) strlcat(x->d86_opnd[1].d86_opnd, "(%di)",
			    OPLEN);
#endif
		break;

	/* si register to accumulator				*/
	case SA:
		wbit = WBIT(opcode2);
#ifdef DIS_TEXT
		dtrace_check_override(x, 0);
		x->d86_numopnds = 2;
		if (addr_size == SIZE64)
			(void) strlcat(x->d86_opnd[0].d86_opnd, "(%rsi)",
			    OPLEN);
		else if (addr_size == SIZE32)
			(void) strlcat(x->d86_opnd[0].d86_opnd, "(%esi)",
			    OPLEN);
		else
			(void) strlcat(x->d86_opnd[0].d86_opnd, "(%si)",
			    OPLEN);
		dtrace_get_operand(x, REG_ONLY, EAX_REGNO, wbit, 1);
#endif
		break;

	/*
	 * single operand, a 16/32 bit displacement
	 */
	case D:
		wbit = LONG_OPND;
		dtrace_disp_opnd(x, wbit, OPSIZE(opnd_size, LONG_OPND), 0);
		NOMEM;
		break;

	/* jmp/call indirect to memory or register operand		*/
	case INM:
#ifdef DIS_TEXT
		(void) strlcat(x->d86_opnd[0].d86_prefix, "*", OPLEN);
#endif
		dtrace_rex_adjust(rex_prefix, mode, NULL, &r_m);
		dtrace_get_operand(x, mode, r_m, LONG_OPND, 0);
		wbit = LONG_OPND;
		break;

	/*
	 * for long jumps and long calls -- a new code segment
	 * register and an offset in IP -- stored in object
	 * code in reverse order. Note - not valid in amd64
	 */
	case SO:
		dtrace_check_override(x, 1);
		wbit = LONG_OPND;
		dtrace_imm_opnd(x, wbit, OPSIZE(opnd_size, LONG_OPND), 1);
#ifdef DIS_TEXT
		x->d86_opnd[1].d86_mode = MODE_SIGNED;
#endif
		/* will now get segment operand */
		dtrace_imm_opnd(x, wbit, 2, 0);
		break;

	/*
	 * jmp/call. single operand, 8 bit displacement.
	 * added to current EIP in 'compofff'
	 */
	case BD:
		dtrace_disp_opnd(x, BYTE_OPND, 1, 0);
		NOMEM;
		break;

	/* single 32/16 bit immediate operand			*/
	case I:
		wbit = LONG_OPND;
		dtrace_imm_opnd(x, wbit, OPSIZE(opnd_size, LONG_OPND), 0);
		break;

	/* single 8 bit immediate operand			*/
	case Ib:
		wbit = LONG_OPND;
		dtrace_imm_opnd(x, wbit, 1, 0);
		break;

	case ENTER:
		wbit = LONG_OPND;
		dtrace_imm_opnd(x, wbit, 2, 0);
		dtrace_imm_opnd(x, wbit, 1, 1);
		switch (opnd_size) {
		case SIZE64:
			x->d86_memsize = (x->d86_opnd[1].d86_value + 1) * 8;
			break;
		case SIZE32:
			x->d86_memsize = (x->d86_opnd[1].d86_value + 1) * 4;
			break;
		case SIZE16:
			x->d86_memsize = (x->d86_opnd[1].d86_value + 1) * 2;
			break;
		}

		break;

	/* 16-bit immediate operand */
	case RET:
		wbit = LONG_OPND;
		dtrace_imm_opnd(x, wbit, 2, 0);
		break;

	/* single 8 bit port operand				*/
	case P:
		dtrace_check_override(x, 0);
		dtrace_imm_opnd(x, BYTE_OPND, 1, 0);
		NOMEM;
		break;

	/* single operand, dx register (variable port instruction) */
	case V:
		x->d86_numopnds = 1;
		dtrace_check_override(x, 0);
#ifdef DIS_TEXT
		(void) strlcat(x->d86_opnd[0].d86_opnd, "(%dx)", OPLEN);
#endif
		NOMEM;
		break;

	/*
	 * The int instruction, which has two forms:
	 * int 3 (breakpoint) or
	 * int n, where n is indicated in the subsequent
	 * byte (format Ib).  The int 3 instruction (opcode 0xCC),
	 * where, although the 3 looks  like an operand,
	 * it is implied by the opcode. It must be converted
	 * to the correct base and output.
	 */
	case INT3:
#ifdef DIS_TEXT
		x->d86_numopnds = 1;
		x->d86_opnd[0].d86_mode = MODE_SIGNED;
		x->d86_opnd[0].d86_value_size = 1;
		x->d86_opnd[0].d86_value = 3;
#endif
		NOMEM;
		break;

	/* single 8 bit immediate operand			*/
	case INTx:
		dtrace_imm_opnd(x, BYTE_OPND, 1, 0);
		NOMEM;
		break;

	/* an unused byte must be discarded */
	case U:
		if (x->d86_get_byte(x->d86_data) < 0)
			goto error;
		x->d86_len++;
		NOMEM;
		break;

	case CBW:
#ifdef DIS_TEXT
		if (opnd_size == SIZE16)
			(void) strlcat(x->d86_mnem, "cbtw", OPLEN);
		else if (opnd_size == SIZE32)
			(void) strlcat(x->d86_mnem, "cwtl", OPLEN);
		else
			(void) strlcat(x->d86_mnem, "cltq", OPLEN);
#endif
		wbit = LONG_OPND;
		NOMEM;
		break;

	case CWD:
#ifdef DIS_TEXT
		if (opnd_size == SIZE16)
			(void) strlcat(x->d86_mnem, "cwtd", OPLEN);
		else if (opnd_size == SIZE32)
			(void) strlcat(x->d86_mnem, "cltd", OPLEN);
		else
			(void) strlcat(x->d86_mnem, "cqtd", OPLEN);
#endif
		wbit = LONG_OPND;
		NOMEM;
		break;

	case XMMSFNC:
		/*
		 * sfence is sfence if mode is REG_ONLY.  If mode isn't
		 * REG_ONLY, mnemonic should be 'clflush'.
		 */
		dtrace_get_modrm(x, &mode, &reg, &r_m);

		/* sfence doesn't take operands */
#ifdef DIS_TEXT
		if (mode == REG_ONLY) {
			(void) strlcat(x->d86_mnem, "sfence", OPLEN);
		} else {
			(void) strlcat(x->d86_mnem, "clflush", OPLEN);
			dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
			dtrace_get_operand(x, mode, r_m, BYTE_OPND, 0);
			NOMEM;
		}
#else
		if (mode != REG_ONLY) {
			dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
			dtrace_get_operand(x, mode, r_m, LONG_OPND, 0);
			NOMEM;
		}
#endif
		break;

	/*
	 * no disassembly, the mnemonic was all there was so go on
	 */
	case NORM:
		if (dp->it_invalid32 && cpu_mode != SIZE64)
			goto error;
		NOMEM;
		/*FALLTHROUGH*/
	case IMPLMEM:
		break;

	case XMMFENCE:
		/*
		 * XRSTOR and LFENCE share the same opcode but differ in mode
		 */
		dtrace_get_modrm(x, &mode, &reg, &r_m);

		if (mode == REG_ONLY) {
			/*
			 * Only the following exact byte sequences are allowed:
			 *
			 * 	0f ae e8	lfence
			 * 	0f ae f0	mfence
			 */
			if ((uint8_t)x->d86_bytes[x->d86_len - 1] != 0xe8 &&
			    (uint8_t)x->d86_bytes[x->d86_len - 1] != 0xf0)
				goto error;
		} else {
#ifdef DIS_TEXT
			(void) strncpy(x->d86_mnem, "xrstor", OPLEN);
#endif
			dtrace_rex_adjust(rex_prefix, mode, &reg, &r_m);
			dtrace_get_operand(x, mode, r_m, BYTE_OPND, 0);
		}
		break;

	/* float reg */
	case F:
#ifdef DIS_TEXT
		x->d86_numopnds = 1;
		(void) strlcat(x->d86_opnd[0].d86_opnd, "%st(X)", OPLEN);
		x->d86_opnd[0].d86_opnd[4] = r_m + '0';
#endif
		NOMEM;
		break;

	/* float reg to float reg, with ret bit present */
	case FF:
		vbit = opcode2 >> 2 & 0x1;	/* vbit = 1: st -> st(i) */
		/*FALLTHROUGH*/
	case FFC:				/* case for vbit always = 0 */
#ifdef DIS_TEXT
		x->d86_numopnds = 2;
		(void) strlcat(x->d86_opnd[1 - vbit].d86_opnd, "%st", OPLEN);
		(void) strlcat(x->d86_opnd[vbit].d86_opnd, "%st(X)", OPLEN);
		x->d86_opnd[vbit].d86_opnd[4] = r_m + '0';
#endif
		NOMEM;
		break;

	/* AVX instructions */
	case VEX_MO:
		/* op(ModR/M.r/m) */
		x->d86_numopnds = 1;
		dtrace_get_modrm(x, &mode, &reg, &r_m);
#ifdef DIS_TEXT
		if ((dp == &dis_opAVX0F[0xA][0xE]) && (reg == 3))
			(void) strncpy(x->d86_mnem, "vstmxcsr", OPLEN);
#endif
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;
	case VEX_RMrX:
	case FMA:
		/* ModR/M.reg := op(VEX.vvvv, ModR/M.r/m) */
		x->d86_numopnds = 3;
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		/*
		 * In classic Intel fashion, the opcodes for all of the FMA
		 * instructions all have two possible mnemonics which vary by
		 * one letter, which is selected based on the value of the wbit.
		 * When wbit is one, they have the 'd' suffix and when 'wbit' is
		 * 0, they have the 's' suffix. Otherwise, the FMA instructions
		 * are all a standard VEX_RMrX.
		 */
#ifdef DIS_TEXT
		if (dp->it_adrmode == FMA) {
			size_t len = strlen(dp->it_name);
			(void) strncpy(x->d86_mnem, dp->it_name, OPLEN);
			if (len + 1 < OPLEN) {
				(void) strncpy(x->d86_mnem + len,
				    vex_W != 0 ? "d" : "s", OPLEN - len);
			}
		}
#endif

		if (mode != REG_ONLY) {
			if ((dp == &dis_opAVXF20F[0x10]) ||
			    (dp == &dis_opAVXF30F[0x10])) {
				/* vmovsd <m64>, <xmm> */
				/* or vmovss <m64>, <xmm> */
				x->d86_numopnds = 2;
				goto L_VEX_MX;
			}
		}

		dtrace_get_operand(x, REG_ONLY, reg, wbit, 2);
		/*
		 * VEX prefix uses the 1's complement form to encode the
		 * XMM/YMM regs
		 */
		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), wbit, 1);

		if ((dp == &dis_opAVXF20F[0x2A]) ||
		    (dp == &dis_opAVXF30F[0x2A])) {
			/*
			 * vcvtsi2si </r,m>, <xmm>, <xmm> or vcvtsi2ss </r,m>,
			 * <xmm>, <xmm>
			 */
			wbit = LONG_OPND;
		}
#ifdef DIS_TEXT
		else if ((mode == REG_ONLY) &&
		    (dp == &dis_opAVX0F[0x1][0x6])) {	/* vmovlhps */
			(void) strncpy(x->d86_mnem, "vmovlhps", OPLEN);
		} else if ((mode == REG_ONLY) &&
		    (dp == &dis_opAVX0F[0x1][0x2])) {	/* vmovhlps */
			(void) strncpy(x->d86_mnem, "vmovhlps", OPLEN);
		}
#endif
		dtrace_get_operand(x, mode, r_m, wbit, 0);

		break;

	case VEX_VRMrX:
		/* ModR/M.reg := op(MODR/M.r/m, VEX.vvvv) */
		x->d86_numopnds = 3;
		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		dtrace_get_operand(x, REG_ONLY, reg, wbit, 2);
		/*
		 * VEX prefix uses the 1's complement form to encode the
		 * XMM/YMM regs
		 */
		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), wbit, 0);

		dtrace_get_operand(x, mode, r_m, wbit, 1);
		break;

	case VEX_SbVM:
		/* ModR/M.reg := op(MODR/M.r/m, VSIB, VEX.vvvv) */
		x->d86_numopnds = 3;
		x->d86_vsib = 1;

		/*
		 * All instructions that use VSIB are currently a mess. See the
		 * comment around the dis_gather_regs_t structure definition.
		 */

		vreg = &dis_vgather[opcode2][vex_W][vex_L];

#ifdef DIS_TEXT
		(void) strncpy(x->d86_mnem, dp->it_name, OPLEN);
		(void) strlcat(x->d86_mnem + strlen(dp->it_name),
		    vreg->dgr_suffix, OPLEN - strlen(dp->it_name));
#endif

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		dtrace_get_operand(x, REG_ONLY, reg, vreg->dgr_arg2, 2);
		/*
		 * VEX prefix uses the 1's complement form to encode the
		 * XMM/YMM regs
		 */
		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), vreg->dgr_arg0,
		    0);
		dtrace_get_operand(x, mode, r_m, vreg->dgr_arg1, 1);
		break;

	case VEX_RRX:
		/* ModR/M.rm := op(VEX.vvvv, ModR/M.reg) */
		x->d86_numopnds = 3;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		if (mode != REG_ONLY) {
			if ((dp == &dis_opAVXF20F[0x11]) ||
			    (dp == &dis_opAVXF30F[0x11])) {
				/* vmovsd <xmm>, <m64> */
				/* or vmovss <xmm>, <m64> */
				x->d86_numopnds = 2;
				goto L_VEX_RM;
			}
		}

		dtrace_get_operand(x, mode, r_m, wbit, 2);
		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), wbit, 1);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 0);
		break;

	case VEX_RMRX:
		/* ModR/M.reg := op(VEX.vvvv, ModR/M.r_m, imm8[7:4]) */
		x->d86_numopnds = 4;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 3);
		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), wbit, 2);
		if (dp == &dis_opAVX660F3A[0x18]) {
			/* vinsertf128 <imm8>, <xmm>, <ymm>, <ymm> */
			dtrace_get_operand(x, mode, r_m, XMM_OPND, 1);
		} else if ((dp == &dis_opAVX660F3A[0x20]) ||
		    (dp == & dis_opAVX660F[0xC4])) {
			/* vpinsrb <imm8>, <reg/mm>, <xmm>, <xmm> */
			/* or vpinsrw <imm8>, <reg/mm>, <xmm>, <xmm> */
			dtrace_get_operand(x, mode, r_m, LONG_OPND, 1);
		} else if (dp == &dis_opAVX660F3A[0x22]) {
			/* vpinsrd/q <imm8>, <reg/mm>, <xmm>, <xmm> */
#ifdef DIS_TEXT
			if (vex_W)
				x->d86_mnem[6] = 'q';
#endif
			dtrace_get_operand(x, mode, r_m, LONG_OPND, 1);
		} else {
			dtrace_get_operand(x, mode, r_m, wbit, 1);
		}

		/* one byte immediate number */
		dtrace_imm_opnd(x, wbit, 1, 0);

		/* vblendvpd, vblendvps, vblendvb use the imm encode the regs */
		if ((dp == &dis_opAVX660F3A[0x4A]) ||
		    (dp == &dis_opAVX660F3A[0x4B]) ||
		    (dp == &dis_opAVX660F3A[0x4C])) {
#ifdef DIS_TEXT
			int regnum = (x->d86_opnd[0].d86_value & 0xF0) >> 4;
#endif
			x->d86_opnd[0].d86_mode = MODE_NONE;
#ifdef DIS_TEXT
			if (vex_L)
				(void) strncpy(x->d86_opnd[0].d86_opnd,
				    dis_YMMREG[regnum], OPLEN);
			else
				(void) strncpy(x->d86_opnd[0].d86_opnd,
				    dis_XMMREG[regnum], OPLEN);
#endif
		}
		break;

	case VEX_MX:
		/* ModR/M.reg := op(ModR/M.rm) */
		x->d86_numopnds = 2;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
L_VEX_MX:

		if ((dp == &dis_opAVXF20F[0xE6]) ||
		    (dp == &dis_opAVX660F[0x5A]) ||
		    (dp == &dis_opAVX660F[0xE6])) {
			/* vcvtpd2dq <ymm>, <xmm> */
			/* or vcvtpd2ps <ymm>, <xmm> */
			/* or vcvttpd2dq <ymm>, <xmm> */
			dtrace_get_operand(x, REG_ONLY, reg, XMM_OPND, 1);
			dtrace_get_operand(x, mode, r_m, wbit, 0);
		} else if ((dp == &dis_opAVXF30F[0xE6]) ||
		    (dp == &dis_opAVX0F[0x5][0xA]) ||
		    (dp == &dis_opAVX660F38[0x13]) ||
		    (dp == &dis_opAVX660F38[0x18]) ||
		    (dp == &dis_opAVX660F38[0x19]) ||
		    (dp == &dis_opAVX660F38[0x58]) ||
		    (dp == &dis_opAVX660F38[0x78]) ||
		    (dp == &dis_opAVX660F38[0x79]) ||
		    (dp == &dis_opAVX660F38[0x59])) {
			/* vcvtdq2pd <xmm>, <ymm> */
			/* or vcvtps2pd <xmm>, <ymm> */
			/* or vcvtph2ps <xmm>, <ymm> */
			/* or vbroadcasts* <xmm>, <ymm> */
			dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);
			dtrace_get_operand(x, mode, r_m, XMM_OPND, 0);
		} else if (dp == &dis_opAVX660F[0x6E]) {
			/* vmovd/q <reg/mem 32/64>, <xmm> */
#ifdef DIS_TEXT
			if (vex_W)
				x->d86_mnem[4] = 'q';
#endif
			dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);
			dtrace_get_operand(x, mode, r_m, LONG_OPND, 0);
		} else {
			dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);
			dtrace_get_operand(x, mode, r_m, wbit, 0);
		}

		break;

	case VEX_MXI:
		/* ModR/M.reg := op(ModR/M.rm, imm8) */
		x->d86_numopnds = 3;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		dtrace_get_operand(x, REG_ONLY, reg, wbit, 2);
		dtrace_get_operand(x, mode, r_m, wbit, 1);

		/* one byte immediate number */
		dtrace_imm_opnd(x, wbit, 1, 0);
		break;

	case VEX_XXI:
		/* VEX.vvvv := op(ModR/M.rm, imm8) */
		x->d86_numopnds = 3;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
#ifdef DIS_TEXT
		(void) strncpy(x->d86_mnem, dis_AVXvgrp7[opcode2 - 1][reg],
		    OPLEN);
#endif
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), wbit, 2);
		dtrace_get_operand(x, REG_ONLY, r_m, wbit, 1);

		/* one byte immediate number */
		dtrace_imm_opnd(x, wbit, 1, 0);
		break;

	case VEX_MR:
		/* ModR/M.reg (reg32/64) := op(ModR/M.rm) */
		if (dp == &dis_opAVX660F[0xC5]) {
			/* vpextrw <imm8>, <xmm>, <reg> */
			x->d86_numopnds = 2;
			vbit = 2;
		} else {
			x->d86_numopnds = 2;
			vbit = 1;
		}

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
		dtrace_get_operand(x, REG_ONLY, reg, LONG_OPND, vbit);
		dtrace_get_operand(x, mode, r_m, wbit, vbit - 1);

		if (vbit == 2)
			dtrace_imm_opnd(x, wbit, 1, 0);

		break;

	case VEX_RRI:
		/* implicit(eflags/r32) := op(ModR/M.reg, ModR/M.rm) */
		x->d86_numopnds = 2;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;

	case VEX_RX:
		/* ModR/M.rm := op(ModR/M.reg) */
		/* vextractf128 || vcvtps2ph */
		if (dp == &dis_opAVX660F3A[0x19] ||
		    dp == &dis_opAVX660F3A[0x1d]) {
			x->d86_numopnds = 3;

			dtrace_get_modrm(x, &mode, &reg, &r_m);
			dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

			dtrace_get_operand(x, mode, r_m, XMM_OPND, 2);
			dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);

			/* one byte immediate number */
			dtrace_imm_opnd(x, wbit, 1, 0);
			break;
		}

		x->d86_numopnds = 2;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 1);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 0);
		break;

	case VEX_RR:
		/* ModR/M.rm := op(ModR/M.reg) */
		x->d86_numopnds = 2;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		if (dp == &dis_opAVX660F[0x7E]) {
			/* vmovd/q <reg/mem 32/64>, <xmm> */
#ifdef DIS_TEXT
			if (vex_W)
				x->d86_mnem[4] = 'q';
#endif
			dtrace_get_operand(x, mode, r_m, LONG_OPND, 1);
		} else
			dtrace_get_operand(x, mode, r_m, wbit, 1);

		dtrace_get_operand(x, REG_ONLY, reg, wbit, 0);
		break;

	case VEX_RRi:
		/* ModR/M.rm := op(ModR/M.reg, imm) */
		x->d86_numopnds = 3;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

#ifdef DIS_TEXT
		if (dp == &dis_opAVX660F3A[0x16]) {
			/* vpextrd/q <imm>, <xmm>, <reg/mem 32/64> */
			if (vex_W)
				x->d86_mnem[6] = 'q';
		}
#endif
		dtrace_get_operand(x, mode, r_m, LONG_OPND, 2);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);

		/* one byte immediate number */
		dtrace_imm_opnd(x, wbit, 1, 0);
		break;
	case VEX_RIM:
		/* ModR/M.rm := op(ModR/M.reg, imm) */
		x->d86_numopnds = 3;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		dtrace_get_operand(x, mode, r_m, XMM_OPND, 2);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);
		/* one byte immediate number */
		dtrace_imm_opnd(x, wbit, 1, 0);
		break;

	case VEX_RM:
		/* ModR/M.rm := op(ModR/M.reg) */
		if (dp == &dis_opAVX660F3A[0x17]) {	/* vextractps */
			x->d86_numopnds = 3;

			dtrace_get_modrm(x, &mode, &reg, &r_m);
			dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

			dtrace_get_operand(x, mode, r_m, LONG_OPND, 2);
			dtrace_get_operand(x, REG_ONLY, reg, wbit, 1);
			/* one byte immediate number */
			dtrace_imm_opnd(x, wbit, 1, 0);
			break;
		}
		x->d86_numopnds = 2;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
L_VEX_RM:
		vbit = 1;
		dtrace_get_operand(x, mode, r_m, wbit, vbit);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, vbit - 1);

		break;

	case VEX_RRM:
		/* ModR/M.rm := op(VEX.vvvv, ModR/M.reg) */
		x->d86_numopnds = 3;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
		dtrace_get_operand(x, mode, r_m, wbit, 2);
		/* VEX use the 1's complement form encode the XMM/YMM regs */
		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), wbit, 1);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 0);
		break;

	case VEX_RMX:
		/* ModR/M.reg := op(VEX.vvvv, ModR/M.rm) */
		x->d86_numopnds = 3;

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);
		dtrace_get_operand(x, REG_ONLY, reg, wbit, 2);
		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), wbit, 1);
		dtrace_get_operand(x, REG_ONLY, r_m, wbit, 0);
		break;

	case VEX_NONE:
#ifdef DIS_TEXT
		if (vex_L)
			(void) strncpy(x->d86_mnem, "vzeroall", OPLEN);
#endif
		break;
	case BLS: {

		/*
		 * The BLS instructions are VEX instructions that are based on
		 * VEX.0F38.F3; however, they are considered special group 17
		 * and like everything else, they use the bits in 3-5 of the
		 * MOD R/M to determine the sub instruction. Unlike many others
		 * like the VMX instructions, these are valid both for memory
		 * and register forms.
		 */

		dtrace_get_modrm(x, &mode, &reg, &r_m);
		dtrace_vex_adjust(vex_byte1, mode, &reg, &r_m);

		switch (reg) {
		case 1:
#ifdef	DIS_TEXT
			blsinstr = "blsr";
#endif
			break;
		case 2:
#ifdef	DIS_TEXT
			blsinstr = "blsmsk";
#endif
			break;
		case 3:
#ifdef	DIS_TEXT
			blsinstr = "blsi";
#endif
			break;
		default:
			goto error;
		}

		x->d86_numopnds = 2;
#ifdef DIS_TEXT
		(void) strncpy(x->d86_mnem, blsinstr, OPLEN);
#endif
		dtrace_get_operand(x, REG_ONLY, (0xF - vex_v), wbit, 1);
		dtrace_get_operand(x, mode, r_m, wbit, 0);
		break;
	}
	/* an invalid op code */
	case AM:
	case DM:
	case OVERRIDE:
	case PREFIX:
	case UNKNOWN:
		NOMEM;
	default:
		goto error;
	} /* end switch */
	if (x->d86_error)
		goto error;

done:
#ifdef DIS_MEM
	/*
	 * compute the size of any memory accessed by the instruction
	 */
	if (x->d86_memsize != 0) {
		return (0);
	} else if (dp->it_stackop) {
		switch (opnd_size) {
		case SIZE16:
			x->d86_memsize = 2;
			break;
		case SIZE32:
			x->d86_memsize = 4;
			break;
		case SIZE64:
			x->d86_memsize = 8;
			break;
		}
	} else if (nomem || mode == REG_ONLY) {
		x->d86_memsize = 0;

	} else if (dp->it_size != 0) {
		/*
		 * In 64 bit mode descriptor table entries
		 * go up to 10 bytes and popf/pushf are always 8 bytes
		 */
		if (x->d86_mode == SIZE64 && dp->it_size == 6)
			x->d86_memsize = 10;
		else if (x->d86_mode == SIZE64 && opcode1 == 0x9 &&
		    (opcode2 == 0xc || opcode2 == 0xd))
			x->d86_memsize = 8;
		else
			x->d86_memsize = dp->it_size;

	} else if (wbit == 0) {
		x->d86_memsize = 1;

	} else if (wbit == LONG_OPND) {
		if (opnd_size == SIZE64)
			x->d86_memsize = 8;
		else if (opnd_size == SIZE32)
			x->d86_memsize = 4;
		else
			x->d86_memsize = 2;

	} else if (wbit == SEG_OPND) {
		x->d86_memsize = 4;

	} else {
		x->d86_memsize = 8;
	}
#endif
	return (0);

error:
#ifdef DIS_TEXT
	(void) strlcat(x->d86_mnem, "undef", OPLEN);
#endif
	return (1);
}

#ifdef DIS_TEXT

/*
 * Some instructions should have immediate operands printed
 * as unsigned integers. We compare against this table.
 */
static char *unsigned_ops[] = {
	"or", "and", "xor", "test", "in", "out", "lcall", "ljmp",
	"rcr", "rcl", "ror", "rol", "shl", "shr", "sal", "psr", "psl",
	0
};


static int
isunsigned_op(char *opcode)
{
	char *where;
	int i;
	int is_unsigned = 0;

	/*
	 * Work back to start of last mnemonic, since we may have
	 * prefixes on some opcodes.
	 */
	where = opcode + strlen(opcode) - 1;
	while (where > opcode && *where != ' ')
		--where;
	if (*where == ' ')
		++where;

	for (i = 0; unsigned_ops[i]; ++i) {
		if (strncmp(where, unsigned_ops[i],
		    strlen(unsigned_ops[i])))
			continue;
		is_unsigned = 1;
		break;
	}
	return (is_unsigned);
}

/*
 * Print a numeric immediate into end of buf, maximum length buflen.
 * The immediate may be an address or a displacement.  Mask is set
 * for address size.  If the immediate is a "small negative", or
 * if it's a negative displacement of any magnitude, print as -<absval>.
 * Respect the "octal" flag.  "Small negative" is defined as "in the
 * interval [NEG_LIMIT, 0)".
 *
 * Also, "isunsigned_op()" instructions never print negatives.
 *
 * Return whether we decided to print a negative value or not.
 */

#define	NEG_LIMIT	-255
enum {IMM, DISP};
enum {POS, TRY_NEG};

static int
print_imm(dis86_t *dis, uint64_t usv, uint64_t mask, char *buf,
    size_t buflen, int disp, int try_neg)
{
	int curlen;
	int64_t sv = (int64_t)usv;
	int octal = dis->d86_flags & DIS_F_OCTAL;

	curlen = strlen(buf);

	if (try_neg == TRY_NEG && sv < 0 &&
	    (disp || sv >= NEG_LIMIT) &&
	    !isunsigned_op(dis->d86_mnem)) {
		dis->d86_sprintf_func(buf + curlen, buflen - curlen,
		    octal ? "-0%llo" : "-0x%llx", (-sv) & mask);
		return (1);
	} else {
		if (disp == DISP)
			dis->d86_sprintf_func(buf + curlen, buflen - curlen,
			    octal ? "+0%llo" : "+0x%llx", usv & mask);
		else
			dis->d86_sprintf_func(buf + curlen, buflen - curlen,
			    octal ? "0%llo" : "0x%llx", usv & mask);
		return (0);

	}
}


static int
log2(int size)
{
	switch (size) {
	case 1: return (0);
	case 2: return (1);
	case 4: return (2);
	case 8: return (3);
	}
	return (0);
}

/* ARGSUSED */
void
dtrace_disx86_str(dis86_t *dis, uint_t mode, uint64_t pc, char *buf,
    size_t buflen)
{
	uint64_t reltgt = 0;
	uint64_t tgt = 0;
	int curlen;
	int (*lookup)(void *, uint64_t, char *, size_t);
	int i;
	int64_t sv;
	uint64_t usv, mask, save_mask, save_usv;
	static uint64_t masks[] =
	    {0xffU, 0xffffU, 0xffffffffU, 0xffffffffffffffffULL};
	save_usv = 0;

	dis->d86_sprintf_func(buf, buflen, "%-6s ", dis->d86_mnem);

	/*
	 * For PC-relative jumps, the pc is really the next pc after executing
	 * this instruction, so increment it appropriately.
	 */
	pc += dis->d86_len;

	for (i = 0; i < dis->d86_numopnds; i++) {
		d86opnd_t *op = &dis->d86_opnd[i];

		if (i != 0)
			(void) strlcat(buf, ",", buflen);

		(void) strlcat(buf, op->d86_prefix, buflen);

		/*
		 * sv is for the signed, possibly-truncated immediate or
		 * displacement; usv retains the original size and
		 * unsignedness for symbol lookup.
		 */

		sv = usv = op->d86_value;

		/*
		 * About masks: for immediates that represent
		 * addresses, the appropriate display size is
		 * the effective address size of the instruction.
		 * This includes MODE_OFFSET, MODE_IPREL, and
		 * MODE_RIPREL.  Immediates that are simply
		 * immediate values should display in the operand's
		 * size, however, since they don't represent addresses.
		 */

		/* d86_addr_size is SIZEnn, which is log2(real size) */
		mask = masks[dis->d86_addr_size];

		/* d86_value_size and d86_imm_bytes are in bytes */
		if (op->d86_mode == MODE_SIGNED ||
		    op->d86_mode == MODE_IMPLIED)
			mask = masks[log2(op->d86_value_size)];

		switch (op->d86_mode) {

		case MODE_NONE:

			(void) strlcat(buf, op->d86_opnd, buflen);
			break;

		case MODE_SIGNED:
		case MODE_IMPLIED:
		case MODE_OFFSET:

			tgt = usv;

			if (dis->d86_seg_prefix)
				(void) strlcat(buf, dis->d86_seg_prefix,
				    buflen);

			if (op->d86_mode == MODE_SIGNED ||
			    op->d86_mode == MODE_IMPLIED) {
				(void) strlcat(buf, "$", buflen);
			}

			if (print_imm(dis, usv, mask, buf, buflen,
			    IMM, TRY_NEG) &&
			    (op->d86_mode == MODE_SIGNED ||
			    op->d86_mode == MODE_IMPLIED)) {

				/*
				 * We printed a negative value for an
				 * immediate that wasn't a
				 * displacement.  Note that fact so we can
				 * print the positive value as an
				 * annotation.
				 */

				save_usv = usv;
				save_mask = mask;
			}
			(void) strlcat(buf, op->d86_opnd, buflen);

			break;

		case MODE_IPREL:
		case MODE_RIPREL:

			reltgt = pc + sv;

			switch (mode) {
			case SIZE16:
				reltgt = (uint16_t)reltgt;
				break;
			case SIZE32:
				reltgt = (uint32_t)reltgt;
				break;
			}

			(void) print_imm(dis, usv, mask, buf, buflen,
			    DISP, TRY_NEG);

			if (op->d86_mode == MODE_RIPREL)
				(void) strlcat(buf, "(%rip)", buflen);
			break;
		}
	}

	/*
	 * The symbol lookups may result in false positives,
	 * particularly on object files, where small numbers may match
	 * the 0-relative non-relocated addresses of symbols.
	 */

	lookup = dis->d86_sym_lookup;
	if (tgt != 0) {
		if ((dis->d86_flags & DIS_F_NOIMMSYM) == 0 &&
		    lookup(dis->d86_data, tgt, NULL, 0) == 0) {
			(void) strlcat(buf, "\t<", buflen);
			curlen = strlen(buf);
			lookup(dis->d86_data, tgt, buf + curlen,
			    buflen - curlen);
			(void) strlcat(buf, ">", buflen);
		}

		/*
		 * If we printed a negative immediate above, print the
		 * positive in case our heuristic was unhelpful
		 */
		if (save_usv) {
			(void) strlcat(buf, "\t<", buflen);
			(void) print_imm(dis, save_usv, save_mask, buf, buflen,
			    IMM, POS);
			(void) strlcat(buf, ">", buflen);
		}
	}

	if (reltgt != 0) {
		/* Print symbol or effective address for reltgt */

		(void) strlcat(buf, "\t<", buflen);
		curlen = strlen(buf);
		lookup(dis->d86_data, reltgt, buf + curlen,
		    buflen - curlen);
		(void) strlcat(buf, ">", buflen);
	}
}

#endif /* DIS_TEXT */
