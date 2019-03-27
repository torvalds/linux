/*	$OpenBSD: mips_opcode.h,v 1.2 1999/01/27 04:46:05 imp Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)mips_opcode.h 8.1 (Berkeley) 6/10/93
 *	JNPR: mips_opcode.h,v 1.1 2006/08/07 05:38:57 katta
 * $FreeBSD$
 */

#ifndef _MACHINE_MIPS_OPCODE_H_
#define	_MACHINE_MIPS_OPCODE_H_

/*
 * Define the instruction formats and opcode values for the
 * MIPS instruction set.
 */
#include <machine/endian.h>

/*
 * Define the instruction formats.
 */
typedef union {
	unsigned word;

#if BYTE_ORDER == BIG_ENDIAN
	struct {
		unsigned op: 6;
		unsigned rs: 5;
		unsigned rt: 5;
		unsigned imm: 16;
	} IType;

	struct {
		unsigned op: 6;
		unsigned target: 26;
	} JType;

	struct {
		unsigned op: 6;
		unsigned rs: 5;
		unsigned rt: 5;
		unsigned rd: 5;
		unsigned shamt: 5;
		unsigned func: 6;
	} RType;

	struct {
		unsigned op: 6;		/* always '0x11' */
		unsigned : 1;		/* always '1' */
		unsigned fmt: 4;
		unsigned ft: 5;
		unsigned fs: 5;
		unsigned fd: 5;
		unsigned func: 6;
	} FRType;
#endif
#if BYTE_ORDER == LITTLE_ENDIAN
	struct {
		unsigned imm: 16;
		unsigned rt: 5;
		unsigned rs: 5;
		unsigned op: 6;
	} IType;

	struct {
		unsigned target: 26;
		unsigned op: 6;
	} JType;

	struct {
		unsigned func: 6;
		unsigned shamt: 5;
		unsigned rd: 5;
		unsigned rt: 5;
		unsigned rs: 5;
		unsigned op: 6;
	} RType;

	struct {
		unsigned func: 6;
		unsigned fd: 5;
		unsigned fs: 5;
		unsigned ft: 5;
		unsigned fmt: 4;
		unsigned : 1;		/* always '1' */
		unsigned op: 6;		/* always '0x11' */
	} FRType;
#endif
} InstFmt;

/* instruction field decoding macros */
#define	MIPS_INST_OPCODE(val)	(val >> 26)
#define	MIPS_INST_RS(val)	((val & 0x03e00000) >> 21)
#define	MIPS_INST_RT(val)	((val & 0x001f0000) >> 16)
#define	MIPS_INST_IMM(val)	((val & 0x0000ffff))

#define	MIPS_INST_RD(val)	((val & 0x0000f800) >> 11)
#define	MIPS_INST_SA(val)	((val & 0x000007c0) >> 6)
#define	MIPS_INST_FUNC(val)	(val & 0x0000003f)

#define	MIPS_INST_INDEX(val)	(val & 0x03ffffff)

/*
 * the mips opcode and function table use a 3bit row and 3bit col
 * number we define the following macro for easy transcribing
 */

#define	MIPS_OPCODE(r, c)	(((r & 0x07) << 3) | (c & 0x07))


/*
 * Values for the 'op' field.
 */
#define	OP_SPECIAL	000
#define	OP_BCOND	001
#define	OP_J		002
#define	OP_JAL		003
#define	OP_BEQ		004
#define	OP_BNE		005
#define	OP_BLEZ		006
#define	OP_BGTZ		007

#define	OP_REGIMM	OP_BCOND

#define	OP_ADDI		010
#define	OP_ADDIU	011
#define	OP_SLTI		012
#define	OP_SLTIU	013
#define	OP_ANDI		014
#define	OP_ORI		015
#define	OP_XORI		016
#define	OP_LUI		017

#define	OP_COP0		020
#define	OP_COP1		021
#define	OP_COP2		022
#define	OP_COP3		023
#define	OP_BEQL		024
#define	OP_BNEL		025
#define	OP_BLEZL	026
#define	OP_BGTZL	027

#define	OP_COP1X	OP_COP3

#define	OP_DADDI	030
#define	OP_DADDIU	031
#define	OP_LDL		032
#define	OP_LDR		033

#define OP_SPECIAL2	034
#define OP_JALX		035

#define OP_SPECIAL3	037

#define	OP_LB		040
#define	OP_LH		041
#define	OP_LWL		042
#define	OP_LW		043
#define	OP_LBU		044
#define	OP_LHU		045
#define	OP_LWR		046
#define	OP_LWU		047

#define	OP_SB		050
#define	OP_SH		051
#define	OP_SWL		052
#define	OP_SW		053
#define	OP_SDL		054
#define	OP_SDR		055
#define	OP_SWR		056
#define	OP_CACHE	057

#define	OP_LL		060
#define	OP_LWC1		061
#define	OP_LWC2		062
#define	OP_LWC3		063
#define	OP_LLD		064
#define	OP_LDC1		065
#define	OP_LDC2		066
#define	OP_LD		067

#define	OP_PREF		OP_LWC3

#define	OP_SC		070
#define	OP_SWC1		071
#define	OP_SWC2		072
#define	OP_SWC3		073
#define	OP_SCD		074
#define	OP_SDC1		075
#define	OP_SDC2		076
#define	OP_SD		077

/*
 * Values for the 'func' field when 'op' == OP_SPECIAL.
 */
#define	OP_SLL		000
#define	OP_MOVCI	001
#define	OP_SRL		002
#define	OP_SRA		003
#define	OP_SLLV		004
#define	OP_SRLV		006
#define	OP_SRAV		007

#define	OP_F_SLL	OP_SLL
#define	OP_F_MOVCI	OP_MOVCI
#define	OP_F_SRL	OP_SRL
#define	OP_F_SRA	OP_SRA
#define	OP_F_SLLV	OP_SLLV
#define	OP_F_SRLV	OP_SRLV
#define	OP_F_SRAV	OP_SRAV

#define	OP_JR		010
#define	OP_JALR		011
#define	OP_MOVZ		012
#define	OP_MOVN		013
#define	OP_SYSCALL	014
#define	OP_BREAK	015
#define	OP_SYNC		017

#define	OP_F_JR		OP_JR
#define	OP_F_JALR	OP_JALR
#define	OP_F_MOVZ	OP_MOVZ
#define	OP_F_MOVN	OP_MOVN
#define	OP_F_SYSCALL	OP_SYSCALL
#define	OP_F_BREAK	OP_BREAK
#define	OP_F_SYNC	OP_SYNC

#define	OP_MFHI		020
#define	OP_MTHI		021
#define	OP_MFLO		022
#define	OP_MTLO		023
#define	OP_DSLLV	024
#define	OP_DSRLV	026
#define	OP_DSRAV	027

#define	OP_F_MFHI	OP_MFHI
#define	OP_F_MTHI	OP_MTHI
#define	OP_F_MFLO	OP_MFLO
#define	OP_F_MTLO	OP_MTLO
#define	OP_F_DSLLV	OP_DSLLV
#define	OP_F_DSRLV	OP_DSRLV
#define	OP_F_DSRAV	OP_DSRAV

#define	OP_MULT		030
#define	OP_MULTU	031
#define	OP_DIV		032
#define	OP_DIVU		033
#define	OP_DMULT	034
#define	OP_DMULTU	035
#define	OP_DDIV		036
#define	OP_DDIVU	037

#define	OP_F_MULT	OP_MULT
#define	OP_F_MULTU	OP_MULTU
#define	OP_F_DIV	OP_DIV
#define	OP_F_DIVU	OP_DIVU
#define	OP_F_DMULT	OP_DMULT
#define	OP_F_DMULTU	OP_DMULTU
#define	OP_F_DDIV	OP_DDIV
#define	OP_F_DDIVU	OP_DDIVU

#define	OP_ADD		040
#define	OP_ADDU		041
#define	OP_SUB		042
#define	OP_SUBU		043
#define	OP_AND		044
#define	OP_OR		045
#define	OP_XOR		046
#define	OP_NOR		047

#define	OP_F_ADD	OP_ADD
#define	OP_F_ADDU	OP_ADDU
#define	OP_F_SUB	OP_SUB
#define	OP_F_SUBU	OP_SUBU
#define	OP_F_AND	OP_AND
#define	OP_F_OR		OP_OR
#define	OP_F_XOR	OP_XOR
#define	OP_F_NOR	OP_NOR

#define	OP_SLT		052
#define	OP_SLTU		053
#define	OP_DADD		054
#define	OP_DADDU	055
#define	OP_DSUB		056
#define	OP_DSUBU	057

#define	OP_F_SLT	OP_SLT
#define	OP_F_SLTU	OP_SLTU
#define	OP_F_DADD	OP_DADD
#define	OP_F_DADDU	OP_DADDU
#define	OP_F_DSUB	OP_DSUB
#define	OP_F_DSUBU	OP_DSUBU

#define	OP_TGE		060
#define	OP_TGEU		061
#define	OP_TLT		062
#define	OP_TLTU		063
#define	OP_TEQ		064
#define	OP_TNE		066

#define	OP_F_TGE	OP_TGE
#define	OP_F_TGEU	OP_TGEU
#define	OP_F_TLT	OP_TLT
#define	OP_F_TLTU	OP_TLTU
#define	OP_F_TEQ	OP_TEQ
#define	OP_F_TNE	OP_TNE

#define	OP_DSLL		070
#define	OP_DSRL		072
#define	OP_DSRA		073
#define	OP_DSLL32	074
#define	OP_DSRL32	076
#define	OP_DSRA32	077

#define	OP_F_DSLL	OP_DSLL
#define	OP_F_DSRL	OP_DSRL
#define	OP_F_DSRA	OP_DSRA
#define	OP_F_DSLL32	OP_DSLL32
#define	OP_F_DSRL32	OP_DSRL32
#define	OP_F_DSRA32	OP_DSRA32

/*
 * The REGIMM - register immediate instructions are further
 * decoded using this table that has 2bit row numbers, hence
 * a need for a new helper macro.
 */

#define	MIPS_ROP(r, c)	((r & 0x03) << 3) | (c & 0x07)

/*
 * Values for the 'func' field when 'op' == OP_BCOND.
 */
#define	OP_BLTZ		000
#define	OP_BGEZ		001
#define	OP_BLTZL	002
#define	OP_BGEZL	003

#define	OP_R_BLTZ	OP_BLTZ
#define	OP_R_BGEZ	OP_BGEZ
#define	OP_R_BLTZL	OP_BLTZL
#define	OP_R_BGEZL	OP_BGEZL

#define	OP_TGEI		010
#define	OP_TGEIU	011
#define	OP_TLTI		012
#define	OP_TLTIU	013
#define	OP_TEQI		014
#define	OP_TNEI		016

#define	OP_R_TGEI	OP_TGEI
#define	OP_R_TGEIU	OP_TGEIU
#define	OP_R_TLTI	OP_TLTI
#define	OP_R_TLTIU	OP_TLTIU
#define	OP_R_TEQI	OP_TEQI
#define	OP_R_TNEI	OP_TNEI

#define	OP_BLTZAL	020
#define	OP_BGEZAL	021
#define	OP_BLTZALL	022
#define	OP_BGEZALL	023

#define	OP_R_BLTZAL	OP_BLTZAL
#define	OP_R_BGEZAL	OP_BGEZAL
#define	OP_R_BLTZALL	OP_BLTZALL
#define	OP_R_BGEZALL	OP_BGEZALL

/*
 * Values for the 'func' field when 'op' == OP_SPECIAL3.
 */
#define	OP_RDHWR	073

/*
 * Values for the 'rs' field when 'op' == OP_COPz.
 */
#define	OP_MF		000
#define	OP_DMF		001
#define	OP_MT		004
#define	OP_DMT		005
#define	OP_BCx		010
#define	OP_BCy		014
#define	OP_CF		002
#define	OP_CT		006

/*
 * Values for the 'rt' field when 'op' == OP_COPz.
 */
#define	COPz_BC_TF_MASK		0x01
#define	COPz_BC_TRUE		0x01
#define	COPz_BC_FALSE		0x00
#define	COPz_BCL_TF_MASK	0x02
#define	COPz_BCL_TRUE		0x02
#define	COPz_BCL_FALSE		0x00

#endif /* !_MACHINE_MIPS_OPCODE_H_ */
