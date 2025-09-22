/*	$OpenBSD: instr.h,v 1.8 2025/07/16 07:15:42 jsg Exp $	*/
/*	$NetBSD: instr.h,v 1.3 2000/01/10 03:53:20 eeh Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)instr.h	8.1 (Berkeley) 6/11/93
 */

/* see also Appendix F of the SPARC version 8 document */
enum IOP { IOP_OP2, IOP_CALL, IOP_reg, IOP_mem };
enum IOP2 { IOP2_UNIMP, IOP2_BPcc, IOP2_Bicc, IOP2_BPr,
	IOP2_SETHI, IOP2_FBPfcc, IOP2_FBfcc, IOP2_CBccc };
enum IOP3_reg {
	IOP3_ADD, IOP3_AND, IOP3_OR, IOP3_XOR,
	IOP3_SUB, IOP3_ANDN, IOP3_ORN, IOP3_XNOR,
	IOP3_ADDX, IOP3_MULX, IOP3_UMUL, IOP3_SMUL,
	IOP3_SUBX, IOP3_UDIVX, IOP3_UDIV, IOP3_SDIV,
	IOP3_ADDcc, IOP3_ANDcc, IOP3_ORcc, IOP3_XORcc,
	IOP3_SUBcc, IOP3_ANDNcc, IOP3_ORNcc, IOP3_XNORcc,
	IOP3_ADDXcc, IOP3_rerr19, IOP3_UMULcc, IOP3_SMULcc,
	IOP3_SUBXcc, IOP3_rerr1d, IOP3_UDIVcc, IOP3_SDIVcc,
	IOP3_TADDcc, IOP3_TSUBcc, IOP3_TADDccTV, IOP3_TSUBccTV,
	IOP3_MULScc, IOP3_SLL, IOP3_SRL, IOP3_SRA,
	IOP3_RDASR_RDY_STBAR, IOP3_RDPSR, IOP3_RDWIM, IOP3_RDTGBR,
	IOP3_MOVcc, IOP3_SDIVX, IOP3_POPC, IOP3_MOVr,
	IOP3_WRASR_WRY, IOP3_WRPSR, IOP3_WRWIM, IOP3_WRTBR,
	IOP3_FPop1, IOP3_FPop2, IOP3_CPop1, IOP3_CPop2,
	IOP3_JMPL, IOP3_RETT, IOP3_Ticc, IOP3_FLUSH,
	IOP3_SAVE, IOP3_RESTORE, IOP3_DONE_RETRY, IOP3_rerr3f
};
enum IOP3_mem {
	IOP3_LD, IOP3_LDUB, IOP3_LDUH, IOP3_LDD,
	IOP3_ST, IOP3_STB, IOP3_STH, IOP3_STD,
	IOP3_LDSW, IOP3_LDSB, IOP3_LDSH, IOP3_LDX,
	IOP3_merr0c, IOP3_LDSTUB, IOP3_STX, IOP3_SWAP,
	IOP3_LDA, IOP3_LDUBA, IOP3_LDUHA, IOP3_LDDA,
	IOP3_STA, IOP3_STBA, IOP3_STHA, IOP3_STDA,
	IOP3_LDSWA, IOP3_LDSBA, IOP3_LDSHA, IOP3_LDXA,
	IOP3_merr1c, IOP3_LDSTUBA, IOP3_STXA, IOP3_SWAPA,
	IOP3_LDF, IOP3_LDFSR, IOP3_LDQF, IOP3_LDDF,
	IOP3_STF, IOP3_STFSR, IOP3_STQF, IOP3_STDF,
	IOP3_merr28, IOP3_merr29, IOP3_merr2a, IOP3_merr2b,
	IOP3_merr2c, IOP3_PREFETCH, IOP3_merr2e, IOP3_merr2f,
	IOP3_LFC, IOP3_LDCSR, IOP3_LDQFA, IOP3_LDDC,
	IOP3_STC, IOP3_STCSR, IOP3_STQFA, IOP3_STDC,
	IOP3_merr38, IOP3_merr39, IOP3_merr3a, IOP3_merr3b,
	IOP3_CASA, IOP3_PREFETCHA, IOP3_CASXA, IOP3_merr3f
};

/*
 * Integer condition codes.
 */
#define	Icc_N	0x0		/* never */
#define	Icc_E	0x1		/* equal (equiv. zero) */
#define	Icc_LE	0x2		/* less or equal */
#define	Icc_L	0x3		/* less */
#define	Icc_LEU	0x4		/* less or equal unsigned */
#define	Icc_CS	0x5		/* carry set (equiv. less unsigned) */
#define	Icc_NEG	0x6		/* negative */
#define	Icc_VS	0x7		/* overflow set */
#define	Icc_A	0x8		/* always */
#define	Icc_NE	0x9		/* not equal (equiv. not zero) */
#define	Icc_G	0xa		/* greater */
#define	Icc_GE	0xb		/* greater or equal */
#define	Icc_GU	0xc		/* greater unsigned */
#define	Icc_CC	0xd		/* carry clear (equiv. gtr or eq unsigned) */
#define	Icc_POS	0xe		/* positive */
#define	Icc_VC	0xf		/* overflow clear */

/*
 * Integer registers.
 */
#define	I_G0	0
#define	I_G1	1
#define	I_G2	2
#define	I_G3	3
#define	I_G4	4
#define	I_G5	5
#define	I_G6	6
#define	I_G7	7
#define	I_O0	8
#define	I_O1	9
#define	I_O2	10
#define	I_O3	11
#define	I_O4	12
#define	I_O5	13
#define	I_O6	14
#define	I_O7	15
#define	I_L0	16
#define	I_L1	17
#define	I_L2	18
#define	I_L3	19
#define	I_L4	20
#define	I_L5	21
#define	I_L6	22
#define	I_L7	23
#define	I_I0	24
#define	I_I1	25
#define	I_I2	26
#define	I_I3	27
#define	I_I4	28
#define	I_I5	29
#define	I_I6	30
#define	I_I7	31

/*
 * An instruction.
 */
union instr {
	int	i_int;			/* as a whole */

	/*
	 * The first level of decoding is to use the top 2 bits.
	 * This gives us one of three `formats', which usually give
	 * a second level of decoding.
	 */
	struct {
		u_int	i_op:2;		/* first-level decode */
		u_int	:30;
	} i_any;

	/*
	 * Format 1 instructions: CALL (undifferentiated).
	 */
	struct {
		u_int	:2;		/* 01 */
		int	i_disp:30;	/* displacement */
	} i_call;

	/*
	 * Format 2 instructions (SETHI, UNIMP, and branches, plus illegal
	 * unused codes).
	 */
	struct {
		u_int	:2;		/* 00 */
		u_int	:5;
		u_int	i_op2:3;	/* second-level decode */
		u_int	:22;
	} i_op2;

	/* UNIMP, SETHI */
	struct {
		u_int	:2;		/* 00 */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op2:3;	/* opcode: UNIMP or SETHI */
		u_int	i_imm:22;	/* immediate value */
	} i_imm22;

	/* branches: Bicc, FBfcc, CBccc */
	struct {
		u_int	:2;		/* 00 */
		u_int	i_annul:1;	/* annul bit */
		u_int	i_cond:4;	/* condition codes */
		u_int	i_op2:3;	/* opcode: {Bi,FBf,CBc}cc */
		int	i_disp:22;	/* branch displacement */
	} i_branch;

	/* more branches: BPcc, FBPfcc */
	struct {
		u_int	:2;		/* 00 */
		u_int	i_annul:1;	/* annul bit */
		u_int	i_cond:4;	/* condition codes */
		u_int	i_op2:3;	/* opcode: {BP,FBPf}cc */
		u_int	i_cc:2;		/* condition code selector */
		u_int	i_pred:1;	/* branch prediction bit */
		int	i_disp:19;	/* branch displacement */
	} i_branch_p;

	/* one last branch: BPr */
	struct {
		u_int	:2;		/* 00 */
		u_int	i_annul:1;	/* annul bit */
		u_int	:1;		/* 0 */
		u_int	i_rcond:4;	/* register condition */
		u_int	:3;		/* 011 */
		int	i_disphi:2;	/* branch displacement, hi bits */
		u_int   i_pred:1;	/* branch prediction bit */
		u_int   i_rs1:1;	/* source register 1 */
		u_int	i_displo:16;	/* branch displacement, lo bits */
	} i_branch_pr;


	/*
	 * Format 3 instructions (memory reference; arithmetic, logical,
	 * shift, and other miscellaneous operations).  The second-level
	 * decode almost always makes use of an `rd' and `rs1', however
	 * (see also IOP3_reg and IOP3_mem).
	 *
	 * Beyond that, the low 14 bits may be broken up in one of three
	 * different ways, if at all:
	 *	1 bit of imm=0 + 8 bits of asi + 5 bits of rs2 [reg & mem]
	 *	1 bit of imm=1 + 13 bits of signed immediate [reg & mem]
	 *	9 bits of coprocessor `opf' opcode + 5 bits of rs2 [reg only]
	 */
	struct {
		u_int	:2;		/* 10 or 11 */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op3:6;	/* second-level decode */
		u_int	i_rs1:5;	/* source register 1 */
		u_int	i_low14:14;	/* varies */
	} i_op3;

	/*
	 * Memory forms.  These set i_op=3 and use simm13 or asi layout.
	 * Memory references without an ASI should use 0, but the actual
	 * ASI field is simply ignored.
	 */
	struct {
		u_int	:2;		/* 11 only */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op3:6;	/* second-level decode (see IOP3_mem) */
		u_int	i_rs1:5;	/* source register 1 */
		u_int	i_i:1;		/* immediate vs asi */
		u_int	i_low13:13;	/* depend on i bit */
	} i_loadstore;

	/*
	 * Memory and register forms.
	 * These come in quite a variety and we do not
	 * attempt to break them down much.
	 */
	struct {
		u_int	:2;		/* 10 or 11 */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op3:6;	/* second-level decode */
		u_int	i_rs1:5;	/* source register 1 */
		u_int	i_i:1;		/* immediate bit (1) */
		int	i_simm13:13;	/* signed immediate */
	} i_simm13;
	struct {
		u_int	:2;		/* 10 or 11 */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op3:6;	/* second-level decode */
		u_int	i_rs1:5;	/* source register 1 */
		u_int	i_i:1;		/* immediate vs asi */
		u_int	i_asi:8;	/* asi */
		u_int	i_rs2:5;	/* source register 2 */
	} i_asi;
	struct {
		u_int	:2;		/* 10 only (register, no memory) */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op3:6;	/* second-level decode (see IOP3_reg) */
		u_int	i_rs1:5;	/* source register 1 */
		u_int	i_opf:9;	/* coprocessor 3rd-level decode */
		u_int	i_rs2:5;	/* source register 2 */
	} i_opf;

	/* 
	 * Format 4 instructions (movcc, fmovr, fmovcc, and tcc).  The
	 * second-level decode almost always makes use of an `rd' and either
	 * `rs1' or `cond'.
	 *
	 * Beyond that, the low 14 bits may be broken up in one of three
	 * different ways, if at all:
	 *	1 bit of imm=0 + 8 bits of asi + 5 bits of rs2 [reg & mem]
	 *	1 bit of imm=1 + 13 bits of signed immediate [reg & mem]
	 * 9 bits of coprocessor `opf' opcode + 5 bits of rs2 [reg only] */
	struct {
		u_int	:2;		/* 10 */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op3:6;	/* second-level decode */
		u_int	i_rs1:5;	/* source register 1 */
		u_int	i_low14:14;	/* varies */
	} i_op4;
	
	/*
	 * Move fp register on condition codes.
	 */
	struct {
		u_int	:2;		/* 10 */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op3:6;	/* second-level decode */
		u_int	:1;
		u_int	i_cond:4;	/* condition */
		u_int	i_opf_cc:3;	/* condition code register */
		u_int	i_opf_low:6;	/* third level decode */
		u_int	i_rs2:5;	/* source register */
	} i_fmovcc;

	/*
	 * Move fp register on integer register.
	 */
	struct {
		u_int	:2;		/* 10 */
		u_int	i_rd:5;		/* destination register */
		u_int	i_op3:6;	/* second-level decode */
		u_int	i_rs1:5;	/* source register 1 */
		u_int	:1;
		u_int	i_rcond:3;	/* register condition */
		u_int	i_opf_low:6;
		u_int	i_rs2:5;	/* source register 2 */
	} i_fmovr;

};

/*
 * Internal macros for building instructions.  These correspond 1-to-1 to
 * the names above.  Note that x << y | z == (x << y) | z.
 */
#define	_I_ANY(op, b)	((op) << 30 | (b))

#define	_I_OP2(high, op2, low) \
		_I_ANY(IOP_OP2, (high) << 25 | (op2) << 22 | (low))
#define	_I_IMM22(rd, op2, imm) \
		_I_ANY(IOP_OP2, (rd) << 25 | (op2) << 22 | (imm))
#define	_I_BRANCH(a, c, op2, disp) \
		_I_ANY(IOP_OP2, (a) << 29 | (c) << 25 | (op2) << 22 | (disp))
#define	_I_FBFCC(a, cond, disp) \
		_I_BRANCH(a, cond, IOP2_FBfcc, disp)
#define	_I_CBCCC(a, cond, disp) \
		_I_BRANCH(a, cond, IOP2_CBccc, disp)

#define	_I_SIMM(simm)		(1 << 13 | ((simm) & 0x1fff))

#define	_I_OP3_GEN(form, rd, op3, rs1, low14) \
		_I_ANY(form, (rd) << 25 | (op3) << 19 | (rs1) << 14 | (low14))
#define	_I_OP3_LS_RAR(rd, op3, rs1, asi, rs2) \
		_I_OP3_GEN(IOP_mem, rd, op3, rs1, (asi) << 5 | (rs2))
#define	_I_OP3_LS_RI(rd, op3, rs1, simm13) \
		_I_OP3_GEN(IOP_mem, rd, op3, rs1, _I_SIMM(simm13))
#define	_I_OP3_LS_RR(rd, op3, rs1, rs2) \
		_I_OP3_GEN(IOP_mem, rd, op3, rs1, rs2)
#define	_I_OP3_R_RAR(rd, op3, rs1, asi, rs2) \
		_I_OP3_GEN(IOP_reg, rd, op3, rs1, (asi) << 5 | (rs2))
#define	_I_OP3_R_RI(rd, op3, rs1, simm13) \
		_I_OP3_GEN(IOP_reg, rd, op3, rs1, _I_SIMM(simm13))
#define	_I_OP3_R_RR(rd, op3, rs1, rs2) \
		_I_OP3_GEN(IOP_reg, rd, op3, rs1, rs2)

#define	I_CALL(d)		_I_ANY(IOP_CALL, d)
#define	I_UNIMP(v)		_I_IMM22(0, IOP2_UNIMP, v)
#define	I_BN(a, d)		_I_BRANCH(a, Icc_N, IOP2_Bicc, d)
#define	I_BE(a, d)		_I_BRANCH(a, Icc_E, IOP2_Bicc, d)
#define	I_BZ(a, d)		_I_BRANCH(a, Icc_E, IOP2_Bicc, d)
#define	I_BLE(a, d)		_I_BRANCH(a, Icc_LE, IOP2_Bicc, d)
#define	I_BL(a, d)		_I_BRANCH(a, Icc_L, IOP2_Bicc, d)
#define	I_BLEU(a, d)		_I_BRANCH(a, Icc_LEU, IOP2_Bicc, d)
#define	I_BCS(a, d)		_I_BRANCH(a, Icc_CS, IOP2_Bicc, d)
#define	I_BLU(a, d)		_I_BRANCH(a, Icc_CS, IOP2_Bicc, d)
#define	I_BNEG(a, d)		_I_BRANCH(a, Icc_NEG, IOP2_Bicc, d)
#define	I_BVS(a, d)		_I_BRANCH(a, Icc_VS, IOP2_Bicc, d)
#define	I_BA(a, d)		_I_BRANCH(a, Icc_A, IOP2_Bicc, d)
#define	I_B(a, d)		_I_BRANCH(a, Icc_A, IOP2_Bicc, d)
#define	I_BNE(a, d)		_I_BRANCH(a, Icc_NE, IOP2_Bicc, d)
#define	I_BNZ(a, d)		_I_BRANCH(a, Icc_NE, IOP2_Bicc, d)
#define	I_BG(a, d)		_I_BRANCH(a, Icc_G, IOP2_Bicc, d)
#define	I_BGE(a, d)		_I_BRANCH(a, Icc_GE, IOP2_Bicc, d)
#define	I_BGU(a, d)		_I_BRANCH(a, Icc_GU, IOP2_Bicc, d)
#define	I_BCC(a, d)		_I_BRANCH(a, Icc_CC, IOP2_Bicc, d)
#define	I_BGEU(a, d)		_I_BRANCH(a, Icc_CC, IOP2_Bicc, d)
#define	I_BPOS(a, d)		_I_BRANCH(a, Icc_POS, IOP2_Bicc, d)
#define	I_BVC(a, d)		_I_BRANCH(a, Icc_VC, IOP2_Bicc, d)
#define	I_SETHI(r, v)		_I_IMM22(r, 4, v)

#define	I_ORri(rd, rs1, imm)	_I_OP3_R_RI(rd, IOP3_OR, rs1, imm)
#define	I_ORrr(rd, rs1, rs2)	_I_OP3_R_RR(rd, IOP3_OR, rs1, rs2)

#define	I_MOVi(rd, imm)		_I_OP3_R_RI(rd, IOP3_OR, I_G0, imm)
#define	I_MOVr(rd, rs)		_I_OP3_R_RR(rd, IOP3_OR, I_G0, rs)

#define	I_RDPSR(rd)		_I_OP3_R_RR(rd, IOP3_RDPSR, 0, 0)

#define	I_JMPLri(rd, rs1, imm)	_I_OP3_R_RI(rd, IOP3_JMPL, rs1, imm)
#define	I_JMPLrr(rd, rs1, rs2)	_I_OP3_R_RR(rd, IOP3_JMPL, rs1, rs2)

/*
 * FPop values.
 */

/* These are in FPop1 space */
#define	FMOVS		0x001
#define	FMOVD		0x002
#define	FMOVQ		0x003
#define	FNEGS		0x005
#define	FNEGD		0x006
#define	FNEGQ		0x007
#define	FABSS		0x009
#define	FABSD		0x00a
#define	FABSQ		0x00b
#define	FSQRTS		0x029
#define	FSQRTD		0x02a
#define	FSQRTQ		0x02b
#define	FADDS		0x041
#define	FADDD		0x042
#define	FADDQ		0x043
#define	FSUBS		0x045
#define	FSUBD		0x046
#define	FSUBQ		0x047
#define	FMULS		0x049
#define	FMULD		0x04a
#define	FMULQ		0x04b
#define	FDIVS		0x04d
#define	FDIVD		0x04e
#define	FDIVQ		0x04f
#define	FSMULD		0x069
#define	FDMULQ		0x06e
#define	FSTOX		0x081
#define	FDTOX		0x082
#define	FQTOX		0x083
#define	FXTOS		0x084
#define	FXTOD		0x088
#define	FXTOQ		0x08c
#define	FITOS		0x0c4
#define	FDTOS		0x0c6
#define	FQTOS		0x0c7
#define	FITOD		0x0c8
#define	FSTOD		0x0c9
#define	FQTOD		0x0cb
#define	FITOQ		0x0cc
#define	FSTOQ		0x0cd
#define	FDTOQ		0x0ce
#define	FSTOI		0x0d1
#define	FDTOI		0x0d2
#define	FQTOI		0x0d3

/* These are in FPop2 space */
#define	FMVFC0S		0x001
#define	FMVFC0D		0x002
#define	FMVFC0Q		0x003
#define	FMOVZS		0x025
#define	FMOVZD		0x026
#define	FMOVZQ		0x027
#define	FMVFC1S		0x041
#define	FMVFC1D		0x042
#define	FMVFC1Q		0x043
#define	FMOVLEZS	0x045
#define	FMOVLEZD	0x046
#define	FMOVLEZQ	0x047
#define	FCMPS		0x051
#define	FCMPD		0x052
#define	FCMPQ		0x053
#define	FCMPES		0x055
#define	FCMPED		0x056
#define	FCMPEQ		0x057
#define	FMOVLZS		0x065
#define	FMOVLZD		0x066
#define	FMOVLZQ		0x067
#define	FMVFC2S		0x081
#define	FMVFC2D		0x082
#define	FMVFC2Q		0x083
#define	FMOVNZS		0x0a5
#define	FMOVNZD		0x0a6
#define	FMOVNZQ		0x0a7
#define	FMVFC3S		0x0c1
#define	FMVFC3D		0x0c2
#define	FMVFC3Q		0x0c3
#define	FMOVGZS		0x0c5
#define	FMOVGZD		0x0c6
#define	FMOVGZQ		0x0c7
#define	FMOVGEZS	0x0e5
#define	FMOVGEZD	0x0e6
#define	FMOVGEZQ	0x0e7
#define	FMVICS		0x101
#define	FMVICD		0x102
#define	FMVICQ		0x103
#define	FMVXCS		0x181
#define	FMVXCD		0x182
#define	FMVXCQ		0x183

/*
 * FPU data types.
 */
#define FTYPE_LNG	-1	/* data = 64-bit signed long integer */		
#define	FTYPE_INT	0	/* data = 32-bit signed integer */
#define	FTYPE_SNG	1	/* data = 32-bit float */
#define	FTYPE_DBL	2	/* data = 64-bit double */
#define	FTYPE_EXT	3	/* data = 128-bit extended (quad-prec) */
