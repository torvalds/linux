/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 David S. Miller, davem@nadzieja.rutgers.edu
 * Copyright (c) 1995 Paul Kranenburg
 * Copyright (c) 2001 Thomas Moestl <tmm@FreeBSD.org>
 * All rights reserved.
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
 *      This product includes software developed by David Miller.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 *
 *	from: NetBSD: db_disasm.c,v 1.9 2000/08/16 11:29:42 pk Exp
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_INSTR_H_
#define _MACHINE_INSTR_H_

/*
 * Definitions for all instruction formats
 */
#define	IF_OP_SHIFT		30
#define	IF_OP_BITS		 2
#define	IF_IMM_SHIFT		 0	/* Immediate/Displacement */

/*
 * Definitions for format 2
 */
#define	IF_F2_RD_SHIFT		25
#define	IF_F2_RD_BITS		 5
#define	IF_F2_A_SHIFT		29
#define	IF_F2_A_BITS		 1
#define	IF_F2_COND_SHIFT	25
#define	IF_F2_COND_BITS		 4
#define	IF_F2_RCOND_SHIFT	25
#define	IF_F2_RCOND_BITS	 3
#define	IF_F2_OP2_SHIFT		22
#define	IF_F2_OP2_BITS		 3
#define	IF_F2_CC1_SHIFT		21
#define	IF_F2_CC1_BITS		 1
#define	IF_F2_CC0_SHIFT		20
#define	IF_F2_CC0_BITS		 1
#define	IF_F2_CC_SHIFT		20	/* CC0 and CC1 combined. */
#define	IF_F2_CC_BITS		 2
#define	IF_F2_D16HI_SHIFT	20
#define	IF_F2_D16HI_BITS	 2
#define	IF_F2_P_SHIFT		19
#define	IF_F2_P_BITS		 1
#define	IF_F2_RS1_SHIFT		14
#define	IF_F2_RS1_BITS		 5

/*
 * Definitions for format 3
 */
#define	IF_F3_OP3_SHIFT		19
#define	IF_F3_OP3_BITS		 6
#define	IF_F3_RD_SHIFT		IF_F2_RD_SHIFT
#define	IF_F3_RD_BITS		IF_F2_RD_BITS
#define	IF_F3_FCN_SHIFT		25
#define	IF_F3_FCN_BITS		 5
#define	IF_F3_CC1_SHIFT		26
#define	IF_F3_CC1_BITS		 1
#define	IF_F3_CC0_SHIFT		25
#define	IF_F3_CC0_BITS		 1
#define	IF_F3_CC_SHIFT		25	/* CC0 and CC1 combined. */
#define	IF_F3_CC_BITS		 2
#define	IF_F3_RS1_SHIFT		IF_F2_RS1_SHIFT
#define	IF_F3_RS1_BITS		IF_F2_RS1_BITS
#define	IF_F3_I_SHIFT		13
#define	IF_F3_I_BITS		 1
#define	IF_F3_X_SHIFT		12
#define	IF_F3_X_BITS		 1
#define	IF_F3_RCOND_SHIFT	10
#define	IF_F3_RCOND_BITS	 3
#define	IF_F3_IMM_ASI_SHIFT	 5
#define	IF_F3_IMM_ASI_BITS	 8
#define	IF_F3_OPF_SHIFT		 5
#define	IF_F3_OPF_BITS		 9
#define	IF_F3_CMASK_SHIFT	 4
#define	IF_F3_CMASK_BITS	 3
#define	IF_F3_RS2_SHIFT		 0
#define	IF_F3_RS2_BITS		 5
#define	IF_F3_SHCNT32_SHIFT	 0
#define	IF_F3_SHCNT32_BITS	 5
#define	IF_F3_SHCNT64_SHIFT	 0
#define	IF_F3_SHCNT64_BITS	 6

/*
 * Definitions for format 4
 */
#define	IF_F4_OP3_SHIFT		IF_F3_OP3_SHIFT
#define	IF_F4_OP3_BITS		IF_F3_OP3_BITS
#define	IF_F4_RD_SHIFT		IF_F2_RD_SHIFT
#define	IF_F4_RD_BITS		IF_F2_RD_BITS
#define	IF_F4_RS1_SHIFT		IF_F2_RS1_SHIFT
#define	IF_F4_RS1_BITS		IF_F2_RS1_BITS
#define	IF_F4_TCOND_SHIFT	IF_F2_COND_SHIFT	/* cond for Tcc */
#define	IF_F4_TCOND_BITS	IF_F2_COND_BITS
#define	IF_F4_CC2_SHIFT		18
#define	IF_F4_CC2_BITS		 1
#define	IF_F4_COND_SHIFT	14
#define	IF_F4_COND_BITS		 4
#define	IF_F4_I_SHIFT		IF_F3_I_SHIFT
#define	IF_F4_I_BITS		IF_F3_I_BITS
#define	IF_F4_OPF_CC_SHIFT	11
#define	IF_F4_OPF_CC_BITS	 3
#define	IF_F4_CC1_SHIFT		12
#define	IF_F4_CC1_BITS		 1
#define	IF_F4_CC0_SHIFT		11
#define	IF_F4_CC0_BITS		 1
#define	IF_F4_RCOND_SHIFT	IF_F3_RCOND_SHIFT
#define	IF_F4_RCOND_BITS	IF_F3_RCOND_BITS
#define	IF_F4_OPF_LOW_SHIFT	 5
#define	IF_F4_RS2_SHIFT		IF_F3_RS2_SHIFT
#define	IF_F4_RS2_BITS		IF_F3_RS2_BITS
#define	IF_F4_SW_TRAP_SHIFT	 0
#define	IF_F4_SW_TRAP_BITS	 7

/*
 * Macros to decode instructions
 */
/* Extract a field */
#define	IF_MASK(s, w)		(((1 << (w)) - 1) << (s))
#define	IF_EXTRACT(x, s, w)	(((x) & IF_MASK((s), (w))) >> (s))
#define	IF_DECODE(x, f) \
	IF_EXTRACT((x), IF_ ## f ## _SHIFT, IF_ ## f ## _BITS)

/* Sign-extend a field of width W */
#define	IF_SEXT(x, w) \
	(((x) & (1L << ((w) - 1))) != 0 ? \
	    (-1L - ((x) ^ ((1L << (w)) - 1))) : (x))

#if 0
/*
 * The following C variant is from db_disassemble.c, and surely faster, but it
 * relies on behaviour that is undefined by the C standard (>> in conjunction
 * with signed negative arguments).
 */
#define	IF_SEXT(v, w)	((((long long)(v)) << (64 - w)) >> (64 - w))
/* Assembler version of the above */
#define	IF_SEXT(v, w) \
	{ u_long t; ( __asm __volatile("sllx %1, %2, %0; srax %0, %2, %0" :
	    "=r" (t) : "r" (v) : "i" (64 - w)); t)}
#endif

/* All instruction formats */
#define	IF_OP(i)		IF_DECODE(i, OP)

/* Instruction format 2 */
#define	IF_F2_RD(i)		IF_DECODE((i), F2_RD)
#define	IF_F2_A(i)		IF_DECODE((i), F2_A)
#define	IF_F2_COND(i)		IF_DECODE((i), F2_COND)
#define	IF_F2_RCOND(i)		IF_DECODE((i), F2_RCOND)
#define	IF_F2_OP2(i)		IF_DECODE((i), F2_OP2)
#define	IF_F2_CC1(i)		IF_DECODE((i), F2_CC1)
#define	IF_F2_CC0(i)		IF_DECODE((i), F2_CC0)
#define	IF_F2_CC(i)		IF_DECODE((i), F2_CC)
#define	IF_F2_D16HI(i)		IF_DECODE((i), F2_D16HI)
#define	IF_F2_P(i)		IF_DECODE((i), F2_P)
#define	IF_F2_RS1(i)		IF_DECODE((i), F2_RS1)

/* Instruction format 3 */
#define	IF_F3_OP3(i)		IF_DECODE((i), F3_OP3)
#define	IF_F3_RD(i)		IF_F2_RD((i))
#define	IF_F3_FCN(i)		IF_DECODE((i), F3_FCN)
#define	IF_F3_CC1(i)		IF_DECODE((i), F3_CC1)
#define	IF_F3_CC0(i)		IF_DECODE((i), F3_CC0)
#define	IF_F3_CC(i)		IF_DECODE((i), F3_CC)
#define	IF_F3_RS1(i)		IF_F2_RS1((i))
#define	IF_F3_I(i)		IF_DECODE((i), F3_I)
#define	IF_F3_X(i)		IF_DECODE((i), F3_X)
#define	IF_F3_RCOND(i)		IF_DECODE((i), F3_RCOND)
#define	IF_F3_IMM_ASI(i)	IF_DECODE((i), F3_IMM_ASI)
#define	IF_F3_OPF(i)		IF_DECODE((i), F3_OPF)
#define	IF_F3_CMASK(i)		IF_DECODE((i), F3_CMASK)
#define	IF_F3_RS2(i)		IF_DECODE((i), F3_RS2)
#define	IF_F3_SHCNT32(i)	IF_DECODE((i), F3_SHCNT32)
#define	IF_F3_SHCNT64(i)	IF_DECODE((i), F3_SHCNT64)

/* Instruction format 4 */
#define	IF_F4_OP3(i)		IF_F3_OP3((i))
#define	IF_F4_RD(i)		IF_F3_RD((i))
#define	IF_F4_TCOND(i)		IF_DECODE((i), F4_TCOND)
#define	IF_F4_RS1(i)		IF_F3_RS1((i))
#define	IF_F4_CC2(i)		IF_DECODE((i), F4_CC2)
#define	IF_F4_COND(i)		IF_DECODE((i), F4_COND)
#define	IF_F4_I(i)		IF_F3_I((i))
#define	IF_F4_OPF_CC(i)		IF_DECODE((i), F4_OPF_CC)
#define	IF_F4_RCOND(i)		IF_F3_RCOND((i))
#define	IF_F4_OPF_LOW(i, w)	IF_EXTRACT((i), IF_F4_OPF_LOW_SHIFT, (w))
#define	IF_F4_RS2(i)		IF_F3_RS2((i))
#define	IF_F4_SW_TRAP(i)	IF_DECODE((i), F4_SW_TRAP)

/* Extract an immediate from an instruction, with an without sign extension */
#define	IF_IMM(i, w)	IF_EXTRACT((i), IF_IMM_SHIFT, (w))
#define	IF_SIMM(i, w)	({ u_long b = (w), x = IF_IMM((i), b); IF_SEXT((x), b); })

/*
 * Macros to encode instructions
 */
#define	IF_INSERT(x, s, w)	(((x) & ((1 << (w)) - 1)) << (s))
#define	IF_ENCODE(x, f) \
	IF_INSERT((x), IF_ ## f ## _SHIFT, IF_ ## f ## _BITS)

/* All instruction formats */
#define	EIF_OP(x)		IF_ENCODE((x), OP)

/* Instruction format 2 */
#define	EIF_F2_RD(x)		IF_ENCODE((x), F2_RD)
#define	EIF_F2_A(x)		IF_ENCODE((x), F2_A)
#define	EIF_F2_COND(x)		IF_ENCODE((x), F2_COND)
#define	EIF_F2_RCOND(x)		IF_ENCODE((x), F2_RCOND)
#define	EIF_F2_OP2(x)		IF_ENCODE((x), F2_OP2)
#define	EIF_F2_CC1(x)		IF_ENCODE((x), F2_CC1)
#define	EIF_F2_CC0(x)		IF_ENCODE((x), F2_CC0)
#define	EIF_F2_D16HI(x)		IF_ENCODE((x), F2_D16HI)
#define	EIF_F2_P(x)		IF_ENCODE((x), F2_P)
#define	EIF_F2_RS1(x)		IF_ENCODE((x), F2_RS1)

/* Instruction format 3 */
#define	EIF_F3_OP3(x)		IF_ENCODE((x), F3_OP3)
#define	EIF_F3_RD(x)		EIF_F2_RD((x))
#define	EIF_F3_FCN(x)		IF_ENCODE((x), F3_FCN)
#define	EIF_F3_CC1(x)		IF_ENCODE((x), F3_CC1)
#define	EIF_F3_CC0(x)		IF_ENCODE((x), F3_CC0)
#define	EIF_F3_RS1(x)		EIF_F2_RS1((x))
#define	EIF_F3_I(x)		IF_ENCODE((x), F3_I)
#define	EIF_F3_X(x)		IF_ENCODE((x), F3_X)
#define	EIF_F3_RCOND(x)		IF_ENCODE((x), F3_RCOND)
#define	EIF_F3_IMM_ASI(x)	IF_ENCODE((x), F3_IMM_ASI)
#define	EIF_F3_OPF(x)		IF_ENCODE((x), F3_OPF)
#define	EIF_F3_CMASK(x)		IF_ENCODE((x), F3_CMASK)
#define	EIF_F3_RS2(x)		IF_ENCODE((x), F3_RS2)
#define	EIF_F3_SHCNT32(x)	IF_ENCODE((x), F3_SHCNT32)
#define	EIF_F3_SHCNT64(x)	IF_ENCODE((x), F3_SHCNT64)

/* Instruction format 4 */
#define	EIF_F4_OP3(x)		EIF_F3_OP3((x))
#define	EIF_F4_RD(x)		EIF_F2_RD((x))
#define	EIF_F4_TCOND(x)		IF_ENCODE((x), F4_TCOND)
#define	EIF_F4_RS1(x)		EIF_F2_RS1((x))
#define	EIF_F4_CC2(x)		IF_ENCODE((x), F4_CC2)
#define	EIF_F4_COND(x)		IF_ENCODE((x), F4_COND)
#define	EIF_F4_I(x)		EIF_F3_I((x))
#define	EIF_F4_OPF_CC(x)	IF_ENCODE((x), F4_OPF_CC)
#define	EIF_F4_RCOND(x)		EIF_F3_RCOND((x))
#define	EIF_F4_OPF_LOW(i, w)	IF_INSERT((x), IF_F4_OPF_CC_SHIFT, (w))
#define	EIF_F4_RS2(x)		EIF_F3_RS2((x))
#define	EIF_F4_SW_TRAP(x)	IF_ENCODE((x), F4_SW_TRAP)

/* Immediates */
#define	EIF_IMM(x, w)	IF_INSERT((x), IF_IMM_SHIFT, (w))
#define	EIF_SIMM(x, w)	IF_EIMM((x), (w))

/*
 * OP field values (specifying the instruction format)
 */
#define	IOP_FORM2		0x00	/* Format 2: sethi, branches */
#define	IOP_CALL		0x01	/* Format 1: call */
#define	IOP_MISC		0x02	/* Format 3 or 4: arith & misc */
#define	IOP_LDST		0x03	/* Format 4: loads and stores */

/*
 * OP2/OP3 values (specifying the actual instruction)
 */
/* OP2 values for format 2 (OP = 0) */
#define	INS0_ILLTRAP		0x00
#define	INS0_BPcc		0x01
#define	INS0_Bicc		0x02
#define	INS0_BPr		0x03
#define	INS0_SETHI	       	0x04	/* with rd = 0 and imm22 = 0, nop */
#define	INS0_FBPfcc		0x05
#define	INS0_FBfcc		0x06
/* undefined			0x07 */

/* OP3 values for Format 3 and 4 (OP = 2) */
#define	INS2_ADD		0x00
#define	INS2_AND		0x01
#define	INS2_OR			0x02
#define	INS2_XOR		0x03
#define	INS2_SUB		0x04
#define	INS2_ANDN		0x05
#define	INS2_ORN		0x06
#define	INS2_XNOR		0x07
#define	INS2_ADDC		0x08
#define	INS2_MULX		0x09
#define	INS2_UMUL		0x0a
#define	INS2_SMUL		0x0b
#define	INS2_SUBC		0x0c
#define	INS2_UDIVX		0x0d
#define	INS2_UDIV		0x0e
#define	INS2_SDIV		0x0f
#define	INS2_ADDcc		0x10
#define	INS2_ANDcc		0x11
#define	INS2_ORcc		0x12
#define	INS2_XORcc		0x13
#define	INS2_SUBcc		0x14
#define	INS2_ANDNcc		0x15
#define	INS2_ORNcc		0x16
#define	INS2_XNORcc		0x17
#define	INS2_ADDCcc		0x18
/* undefined			0x19 */
#define	INS2_UMULcc		0x1a
#define	INS2_SMULcc		0x1b
#define	INS2_SUBCcc		0x1c
/* undefined			0x1d */
#define	INS2_UDIVcc		0x1e
#define	INS2_SDIVcc		0x1f
#define	INS2_TADDcc		0x20
#define	INS2_TSUBcc		0x21
#define	INS2_TADDccTV		0x22
#define	INS2_TSUBccTV		0x23
#define	INS2_MULScc		0x24
#define	INS2_SSL		0x25	/* SLLX when IF_X(i) == 1 */
#define	INS2_SRL		0x26	/* SRLX when IF_X(i) == 1 */
#define	INS2_SRA		0x27	/* SRAX when IF_X(i) == 1 */
#define	INS2_RD			0x28	/* and MEMBAR, STBAR */
/* undefined			0x29 */
#define	INS2_RDPR		0x2a
#define	INS2_FLUSHW		0x2b
#define	INS2_MOVcc		0x2c
#define	INS2_SDIVX		0x2d
#define	INS2_POPC		0x2e	/* undefined if IF_RS1(i) != 0 */
#define	INS2_MOVr		0x2f
#define	INS2_WR			0x30	/* and SIR */
#define	INS2_SV_RSTR		0x31	/* saved, restored */
#define	INS2_WRPR		0x32
/* undefined			0x33 */
#define	INS2_FPop1		0x34	/* further encoded in opf field */
#define	INS2_FPop2		0x35	/* further encoded in opf field */
#define	INS2_IMPLDEP1		0x36
#define	INS2_IMPLDEP2		0x37
#define	INS2_JMPL		0x38
#define	INS2_RETURN		0x39
#define	INS2_Tcc		0x3a
#define	INS2_FLUSH		0x3b
#define	INS2_SAVE		0x3c
#define	INS2_RESTORE		0x3d
#define	INS2_DONE_RETR		0x3e	/* done, retry */
/* undefined			0x3f */

/* OP3 values for format 3 (OP = 3) */
#define	INS3_LDUW		0x00
#define	INS3_LDUB		0x01
#define	INS3_LDUH		0x02
#define	INS3_LDD		0x03
#define	INS3_STW		0x04
#define	INS3_STB		0x05
#define	INS3_STH		0x06
#define	INS3_STD		0x07
#define	INS3_LDSW		0x08
#define	INS3_LDSB		0x09
#define	INS3_LDSH		0x0a
#define	INS3_LDX		0x0b
/* undefined			0x0c */
#define	INS3_LDSTUB		0x0d
#define	INS3_STX		0x0e
#define	INS3_SWAP		0x0f
#define	INS3_LDUWA		0x10
#define	INS3_LDUBA		0x11
#define	INS3_LDUHA		0x12
#define	INS3_LDDA		0x13
#define	INS3_STWA		0x14
#define	INS3_STBA		0x15
#define	INS3_STHA		0x16
#define	INS3_STDA		0x17
#define	INS3_LDSWA		0x18
#define	INS3_LDSBA		0x19
#define	INS3_LDSHA		0x1a
#define	INS3_LDXA		0x1b
/* undefined			0x1c */
#define	INS3_LDSTUBA		0x1d
#define	INS3_STXA		0x1e
#define	INS3_SWAPA		0x1f
#define	INS3_LDF		0x20
#define	INS3_LDFSR		0x21	/* and LDXFSR */
#define	INS3_LDQF		0x22
#define	INS3_LDDF		0x23
#define	INS3_STF		0x24
#define	INS3_STFSR		0x25	/* and STXFSR */
#define	INS3_STQF		0x26
#define	INS3_STDF		0x27
/* undefined			0x28 - 0x2c */
#define	INS3_PREFETCH		0x2d
/* undefined			0x2e - 0x2f */
#define	INS3_LDFA		0x30
/* undefined			0x31 */
#define	INS3_LDQFA		0x32
#define	INS3_LDDFA		0x33
#define	INS3_STFA		0x34
/* undefined			0x35 */
#define	INS3_STQFA		0x36
#define	INS3_STDFA		0x37
/* undefined			0x38 - 0x3b */
#define	INS3_CASA		0x39
#define	INS3_PREFETCHA		0x3a
#define	INS3_CASXA		0x3b

/*
 * OPF values (floating point instructions, IMPLDEP)
 */
/*
 * These values are or'ed to the FPop values to get the instructions.
 * They describe the operand type(s).
 */
#define	INSFP_i			0x000	/* 32-bit int */
#define	INSFP_s			0x001	/* 32-bit single */
#define	INSFP_d			0x002	/* 64-bit double */
#define	INSFP_q			0x003	/* 128-bit quad */
/* FPop1. The comments give the types for which this instruction is defined. */
#define	INSFP1_FMOV		0x000	/* s, d, q */
#define	INSFP1_FNEG		0x004	/* s, d, q */
#define	INSFP1_FABS		0x008	/* s, d, q */
#define	INSFP1_FSQRT		0x028	/* s, d, q */
#define	INSFP1_FADD		0x040	/* s, d, q */
#define	INSFP1_FSUB		0x044	/* s, d, q */
#define	INSFP1_FMUL		0x048	/* s, d, q */
#define	INSFP1_FDIV		0x04c	/* s, d, q */
#define	INSFP1_FsMULd		0x068	/* s */
#define	INSFP1_FdMULq		0x06c	/* d */
#define	INSFP1_FTOx		0x080	/* s, d, q */
#define	INSFP1_FxTOs		0x084	/* special: i only */
#define	INSFP1_FxTOd		0x088	/* special: i only */
#define	INSFP1_FxTOq		0x08c	/* special: i only */
#define	INSFP1_FTOs		0x0c4	/* i, d, q */
#define	INSFP1_FTOd		0x0c8	/* i, s, q */
#define	INSFP1_FTOq		0x0cc	/* i, s, d */
#define	INSFP1_FTOi		0x0d0	/* i, s, d */

/* FPop2 */
#define	INSFP2_FMOV_CCMUL	0x40
#define	INSFP2_FMOV_CCOFFS	0x00
/* Use the IFCC_* constants for cc. Operand types: s, d, q */
#define	INSFP2_FMOV_CC(cc)	((cc) * INSFP2_FMOV_CCMUL + INSFP2_FMOV_CCOFFS)
#define	INSFP2_FMOV_RCMUL	0x20
#define	INSFP2_FMOV_RCOFFS	0x04
/* Use the IRCOND_* constants for rc. Operand types: s, d, q */
#define	INSFP2_FMOV_RC(rc)	((rc) * INSFP2_FMOV_RCMUL + INSFP2_FMOV_RCOFFS)
#define	INSFP2_FCMP		0x050	/* s, d, q */
#define	INSFP2_FCMPE		0x054	/* s, d, q */

/* Decode 5-bit register field into 6-bit number (for doubles and quads). */
#define	INSFPdq_RN(rn)		(((rn) & ~1) | (((rn) & 1) << 5))

/* IMPLDEP1 for Sun UltraSparc */
#define	IIDP1_EDGE8		0x00
#define	IIDP1_EDGE8N		0x01	/* US-III */
#define	IIDP1_EDGE8L		0x02
#define	IIDP1_EDGE8LN		0x03	/* US-III */
#define	IIDP1_EDGE16		0x04
#define	IIDP1_EDGE16N		0x05	/* US-III */
#define	IIDP1_EDGE16L		0x06
#define	IIDP1_EDGE16LN		0x07	/* US-III */
#define	IIDP1_EDGE32		0x08
#define	IIDP1_EDGE32N		0x09	/* US-III */
#define	IIDP1_EDGE32L		0x0a
#define	IIDP1_EDGE32LN		0x0b	/* US-III */
#define	IIDP1_ARRAY8		0x10
#define	IIDP1_ARRAY16		0x12
#define	IIDP1_ARRAY32		0x14
#define	IIDP1_ALIGNADDRESS	0x18
#define	IIDP1_BMASK		0x19	/* US-III */
#define	IIDP1_ALIGNADDRESS_L	0x1a
#define	IIDP1_FCMPLE16		0x20
#define	IIDP1_FCMPNE16		0x22
#define	IIDP1_FCMPLE32		0x24
#define	IIDP1_FCMPNE32		0x26
#define	IIDP1_FCMPGT16		0x28
#define	IIDP1_FCMPEQ16		0x2a
#define	IIDP1_FCMPGT32		0x2c
#define	IIDP1_FCMPEQ32		0x2e
#define	IIDP1_FMUL8x16		0x31
#define	IIDP1_FMUL8x16AU	0x33
#define	IIDP1_FMUL8X16AL	0x35
#define	IIDP1_FMUL8SUx16	0x36
#define	IIDP1_FMUL8ULx16	0x37
#define	IIDP1_FMULD8SUx16	0x38
#define	IIDP1_FMULD8ULx16	0x39
#define	IIDP1_FPACK32		0x3a
#define	IIDP1_FPACK16		0x3b
#define	IIDP1_FPACKFIX		0x3d
#define	IIDP1_PDIST		0x3e
#define	IIDP1_FALIGNDATA	0x48
#define	IIDP1_FPMERGE		0x4b
#define	IIDP1_BSHUFFLE		0x4c	/* US-III */
#define	IIDP1_FEXPAND		0x4d
#define	IIDP1_FPADD16		0x50
#define	IIDP1_FPADD16S		0x51
#define	IIDP1_FPADD32		0x52
#define	IIDP1_FPADD32S		0x53
#define	IIDP1_SUB16		0x54
#define	IIDP1_SUB16S		0x55
#define	IIDP1_SUB32		0x56
#define	IIDP1_SUB32S		0x57
#define	IIDP1_FZERO		0x60
#define	IIDP1_FZEROS		0x61
#define	IIDP1_FNOR		0x62
#define	IIDP1_FNORS		0x63
#define	IIDP1_FANDNOT2		0x64
#define	IIDP1_FANDNOT2S		0x65
#define	IIDP1_NOT2		0x66
#define	IIDP1_NOT2S		0x67
#define	IIDP1_FANDNOT1		0x68
#define	IIDP1_FANDNOT1S		0x69
#define	IIDP1_FNOT1		0x6a
#define	IIDP1_FNOT1S		0x6b
#define	IIDP1_FXOR		0x6c
#define	IIDP1_FXORS		0x6d
#define	IIDP1_FNAND		0x6e
#define	IIDP1_FNANDS		0x6f
#define	IIDP1_FAND		0x70
#define	IIDP1_FANDS		0x71
#define	IIDP1_FXNOR		0x72
#define	IIDP1_FXNORS		0x73
#define	IIDP1_FSRC1		0x74
#define	IIDP1_FSRC1S		0x75
#define	IIDP1_FORNOT2		0x76
#define	IIDP1_FORNOT2S		0x77
#define	IIDP1_FSRC2		0x78
#define	IIDP1_FSRC2S		0x79
#define	IIDP1_FORNOT1		0x7a
#define	IIDP1_FORNOT1S		0x7b
#define	IIDP1_FOR		0x7c
#define	IIDP1_FORS		0x7d
#define	IIDP1_FONE		0x7e
#define	IIDP1_FONES		0x7f
#define	IIDP1_SHUTDOWN		0x80
#define	IIDP1_SIAM		0x81	/* US-III */

/*
 * Instruction modifiers
 */
/* cond values for integer ccr's */
#define	IICOND_N		0x00
#define	IICOND_E		0x01
#define	IICOND_LE		0x02
#define	IICOND_L		0x03
#define	IICOND_LEU		0x04
#define	IICOND_CS		0x05
#define	IICOND_NEG		0x06
#define	IICOND_VS		0x07
#define	IICOND_A		0x08
#define	IICOND_NE		0x09
#define	IICOND_G		0x0a
#define	IICOND_GE		0x0b
#define	IICOND_GU		0x0c
#define	IICOND_CC		0x0d
#define	IICOND_POS		0x0e
#define	IICOND_VC		0x0f

/* cond values for fp ccr's */
#define	IFCOND_N		0x00
#define	IFCOND_NE		0x01
#define	IFCOND_LG		0x02
#define	IFCOND_UL		0x03
#define	IFCOND_L		0x04
#define	IFCOND_UG		0x05
#define	IFCOND_G		0x06
#define	IFCOND_U		0x07
#define	IFCOND_A		0x08
#define	IFCOND_E		0x09
#define	IFCOND_UE		0x0a
#define	IFCOND_GE		0x0b
#define	IFCOND_UGE		0x0c
#define	IFCOND_LE		0x0d
#define	IFCOND_ULE		0x0e
#define	IFCOND_O		0x0f

/* rcond values for BPr, MOVr, FMOVr */
#define	IRCOND_Z		0x01
#define	IRCOND_LEZ		0x02
#define	IRCOND_LZ		0x03
#define	IRCOND_NZ		0x05
#define	IRCOND_GZ		0x06
#define	IRCOND_GEZ		0x07

/* cc values for MOVcc and FMOVcc */
#define	IFCC_ICC		0x04
#define	IFCC_XCC		0x06
/* if true, the lower 2 bits are the fcc number */
#define	IFCC_FCC(c)		((c) & 3)
#define	IFCC_GET_FCC(c)		((c) & 3)
#define	IFCC_ISFCC(c)		(((c) & 4) == 0)

/* cc values for BPc and Tcc */
#define	IBCC_ICC		0x00
#define	IBCC_XCC		0x02

/*
 * Integer registers
 */
#define	IREG_G0			0x00
#define	IREG_O0			0x08
#define	IREG_L0			0x10
#define	IREQ_I0			0x18

#endif /* !_MACHINE_INSTR_H_ */
