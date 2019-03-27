/*-
 * Copyright (c) 2016-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>

#include <machine/encoding.h>

#define	X_RA	1
#define	X_SP	2
#define	X_GP	3
#define	X_TP	4
#define	X_T0	5
#define	X_T1	6
#define	X_T2	7
#define	X_T3	28

#define	RD_SHIFT	7
#define	RD_MASK		(0x1f << RD_SHIFT)
#define	RS1_SHIFT	15
#define	RS1_MASK	(0x1f << RS1_SHIFT)
#define	RS2_SHIFT	20
#define	RS2_MASK	(0x1f << RS2_SHIFT)
#define	IMM_SHIFT	20
#define	IMM_MASK	(0xfff << IMM_SHIFT)

static char *reg_name[32] = {
	"zero",	"ra",	"sp",	"gp",	"tp",	"t0",	"t1",	"t2",
	"s0",	"s1",	"a0",	"a1",	"a2",	"a3",	"a4",	"a5",
	"a6",	"a7",	"s2",	"s3",	"s4",	"s5",	"s6",	"s7",
	"s8",	"s9",	"s10",	"s11",	"t3",	"t4",	"t5",	"t6"
};

static char *fp_reg_name[32] = {
	"ft0", "ft1", "ft2",  "ft3",  "ft4", "ft5", "ft6",  "ft7",
	"fs0", "fs1", "fa0",  "fa1",  "fa2", "fa3", "fa4",  "fa5",
	"fa6", "fa7", "fs2",  "fs3",  "fs4", "fs5", "fs6",  "fs7",
	"fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
};

struct riscv_op {
	char *name;
	char *fmt;
	int match;
	int mask;
	int (*match_func)(struct riscv_op *op, uint32_t insn);
};

static int
m_op(struct riscv_op *op, uint32_t insn)
{

	if (((insn ^ op->match) & op->mask) == 0)
		return (1);

	return (0);
}

static struct riscv_op riscv_opcodes[] = {
	/* Aliases first */
	{"ret","", MATCH_JALR | (X_RA << RS1_SHIFT),
	    MASK_JALR | RD_MASK | RS1_MASK | IMM_MASK, m_op },

	{ "beq",	"s,t,p", 	MATCH_BEQ, MASK_BEQ,		m_op },
	{ "bne",	"s,t,p", 	MATCH_BNE, MASK_BNE,		m_op },
	{ "blt",	"s,t,p", 	MATCH_BLT, MASK_BLT,		m_op },
	{ "bge",	"s,t,p", 	MATCH_BGE, MASK_BGE,		m_op },
	{ "bltu",	"s,t,p", 	MATCH_BLTU, MASK_BLTU,		m_op },
	{ "bgeu",	"s,t,p", 	MATCH_BGEU, MASK_BGEU,		m_op },
	{ "jalr",	"d,o(s)", 	MATCH_JALR, MASK_JALR,		m_op },
	{ "jal",	"d,a", 		MATCH_JAL, MASK_JAL,		m_op },
	{ "lui",	"d,u", 		MATCH_LUI, MASK_LUI,		m_op },
	{ "auipc",	"d,u", 		MATCH_AUIPC, MASK_AUIPC,	m_op },
	{ "addi",	"d,s,j", 	MATCH_ADDI, MASK_ADDI,		m_op },
	{ "slli",	"d,s,>", 	MATCH_SLLI, MASK_SLLI,		m_op },
	{ "slti",	"d,s,j", 	MATCH_SLTI, MASK_SLTI,		m_op },
	{ "sltiu",	"d,s,j", 	MATCH_SLTIU, MASK_SLTIU,	m_op },
	{ "xori",	"d,s,j", 	MATCH_XORI, MASK_XORI,		m_op },
	{ "srli",	"d,s,>", 	MATCH_SRLI, MASK_SRLI,		m_op },
	{ "srai",	"d,s,>", 	MATCH_SRAI, MASK_SRAI,		m_op },
	{ "ori",	"d,s,j", 	MATCH_ORI, MASK_ORI,		m_op },
	{ "andi",	"d,s,j", 	MATCH_ANDI, MASK_ANDI,		m_op },
	{ "add",	"d,s,t", 	MATCH_ADD, MASK_ADD,		m_op },
	{ "sub",	"d,s,t", 	MATCH_SUB, MASK_SUB,		m_op },
	{ "sll",	"d,s,t", 	MATCH_SLL, MASK_SLL,		m_op },
	{ "slt",	"d,s,t", 	MATCH_SLT, MASK_SLT,		m_op },
	{ "sltu",	"d,s,t", 	MATCH_SLTU, MASK_SLTU,		m_op },
	{ "xor",	"d,s,t", 	MATCH_XOR, MASK_XOR,		m_op },
	{ "srl",	"d,s,t", 	MATCH_SRL, MASK_SRL,		m_op },
	{ "sra",	"d,s,t", 	MATCH_SRA, MASK_SRA,		m_op },
	{ "or",		"d,s,t", 	MATCH_OR, MASK_OR,		m_op },
	{ "and",	"d,s,t", 	MATCH_AND, MASK_AND,		m_op },
	{ "addiw",	"d,s,j", 	MATCH_ADDIW, MASK_ADDIW,	m_op },
	{ "slliw",	"d,s,<", 	MATCH_SLLIW, MASK_SLLIW,	m_op },
	{ "srliw",	"d,s,<", 	MATCH_SRLIW, MASK_SRLIW,	m_op },
	{ "sraiw",	"d,s,<", 	MATCH_SRAIW, MASK_SRAIW,	m_op },
	{ "addw",	"d,s,t", 	MATCH_ADDW, MASK_ADDW,		m_op },
	{ "subw",	"d,s,t", 	MATCH_SUBW, MASK_SUBW,		m_op },
	{ "sllw",	"d,s,t", 	MATCH_SLLW, MASK_SLLW,		m_op },
	{ "srlw",	"d,s,t", 	MATCH_SRLW, MASK_SRLW,		m_op },
	{ "sraw",	"d,s,t", 	MATCH_SRAW, MASK_SRAW,		m_op },
	{ "lb",		"d,o(s)", 	MATCH_LB, MASK_LB,		m_op },
	{ "lh",		"d,o(s)", 	MATCH_LH, MASK_LH,		m_op },
	{ "lw",		"d,o(s)", 	MATCH_LW, MASK_LW,		m_op },
	{ "ld",		"d,o(s)", 	MATCH_LD, MASK_LD,		m_op },
	{ "lbu",	"d,o(s)", 	MATCH_LBU, MASK_LBU,		m_op },
	{ "lhu",	"d,o(s)", 	MATCH_LHU, MASK_LHU,		m_op },
	{ "lwu",	"d,o(s)", 	MATCH_LWU, MASK_LWU,		m_op },
	{ "sb",		"t,q(s)", 	MATCH_SB, MASK_SB,		m_op },
	{ "sh",		"t,q(s)", 	MATCH_SH, MASK_SH,		m_op },
	{ "sw",		"t,q(s)", 	MATCH_SW, MASK_SW,		m_op },
	{ "sd",		"t,q(s)", 	MATCH_SD, MASK_SD,		m_op },
	{ "fence",	"P,Q",		MATCH_FENCE, MASK_FENCE,	m_op },
	{ "fence.i",	"",		MATCH_FENCE_I, MASK_FENCE_I,	m_op },
	{ "mul",	"d,s,t", 	MATCH_MUL, MASK_MUL,		m_op },
	{ "mulh",	"d,s,t", 	MATCH_MULH, MASK_MULH,		m_op },
	{ "mulhsu",	"d,s,t", 	MATCH_MULHSU, MASK_MULHSU,	m_op },
	{ "mulhu",	"d,s,t", 	MATCH_MULHU, MASK_MULHU,	m_op },
	{ "div",	"d,s,t", 	MATCH_DIV, MASK_DIV,		m_op },
	{ "divu",	"d,s,t", 	MATCH_DIVU, MASK_DIVU,		m_op },
	{ "rem",	"d,s,t", 	MATCH_REM, MASK_REM,		m_op },
	{ "remu",	"d,s,t", 	MATCH_REMU, MASK_REMU,		m_op },
	{ "mulw",	"d,s,t", 	MATCH_MULW, MASK_MULW,		m_op },
	{ "divw",	"d,s,t", 	MATCH_DIVW, MASK_DIVW,		m_op },
	{ "divuw",	"d,s,t", 	MATCH_DIVUW, MASK_DIVUW,	m_op },
	{ "remw",	"d,s,t", 	MATCH_REMW, MASK_REMW,		m_op },
	{ "remuw",	"d,s,t", 	MATCH_REMUW, MASK_REMUW,	m_op },
	{ "amoadd.w",	"d,t,0(s)", 	MATCH_AMOADD_W, MASK_AMOADD_W,	m_op },
	{ "amoxor.w",	"d,t,0(s)", 	MATCH_AMOXOR_W, MASK_AMOXOR_W,	m_op },
	{ "amoor.w",	"d,t,0(s)", 	MATCH_AMOOR_W, MASK_AMOOR_W,	m_op },
	{ "amoand.w",	"d,t,0(s)", 	MATCH_AMOAND_W, MASK_AMOAND_W,	m_op },
	{ "amomin.w",	"d,t,0(s)", 	MATCH_AMOMIN_W, MASK_AMOMIN_W,	m_op },
	{ "amomax.w",	"d,t,0(s)", 	MATCH_AMOMAX_W, MASK_AMOMAX_W,	m_op },
	{ "amominu.w",	"d,t,0(s)", 	MATCH_AMOMINU_W, MASK_AMOMINU_W,m_op },
	{ "amomaxu.w",	"d,t,0(s)", 	MATCH_AMOMAXU_W, MASK_AMOMAXU_W,m_op },
	{ "amoswap.w",	"d,t,0(s)", 	MATCH_AMOSWAP_W, MASK_AMOSWAP_W,m_op },
	{ "lr.w",	"d,0(s)", 	MATCH_LR_W, MASK_LR_W,		m_op },
	{ "sc.w",	"d,t,0(s)", 	MATCH_SC_W, MASK_SC_W,		m_op },
	{ "amoadd.d",	"d,t,0(s)", 	MATCH_AMOADD_D, MASK_AMOADD_D,	m_op },
	{ "amoxor.d",	"d,t,0(s)", 	MATCH_AMOXOR_D, MASK_AMOXOR_D,	m_op },
	{ "amoor.d",	"d,t,0(s)", 	MATCH_AMOOR_D, MASK_AMOOR_D,	m_op },
	{ "amoand.d",	"d,t,0(s)", 	MATCH_AMOAND_D, MASK_AMOAND_D,	m_op },
	{ "amomin.d",	"d,t,0(s)", 	MATCH_AMOMIN_D, MASK_AMOMIN_D,	m_op },
	{ "amomax.d",	"d,t,0(s)", 	MATCH_AMOMAX_D, MASK_AMOMAX_D,	m_op },
	{ "amominu.d",	"d,t,0(s)", 	MATCH_AMOMINU_D, MASK_AMOMINU_D,m_op },
	{ "amomaxu.d",	"d,t,0(s)", 	MATCH_AMOMAXU_D, MASK_AMOMAXU_D,m_op },
	{ "amoswap.d",	"d,t,0(s)", 	MATCH_AMOSWAP_D, MASK_AMOSWAP_D,m_op },
	{ "lr.d",	"d,0(s)", 	MATCH_LR_D, MASK_LR_D,		m_op },
	{ "sc.d",	"d,t,0(s)", 	MATCH_SC_D, MASK_SC_D,		m_op },
	{ "ecall",	"", 		MATCH_ECALL, MASK_ECALL,	m_op },
	{ "ebreak",	"", 		MATCH_EBREAK, MASK_EBREAK,	m_op },
	{ "uret",	"", 		MATCH_URET, MASK_URET,		m_op },
	{ "sret",	"", 		MATCH_SRET, MASK_SRET,		m_op },
	{ "mret",	"", 		MATCH_MRET, MASK_MRET,		m_op },
	{ "dret",	"", 		MATCH_DRET, MASK_DRET,		m_op },
	{ "sfence.vma",	"", 	MATCH_SFENCE_VMA, MASK_SFENCE_VMA,	m_op },
	{ "wfi",	"", 		MATCH_WFI, MASK_WFI,		m_op },
	{ "csrrw",	"d,E,s", 	MATCH_CSRRW, MASK_CSRRW,	m_op },
	{ "csrrs",	"d,E,s", 	MATCH_CSRRS, MASK_CSRRS,	m_op },
	{ "csrrc",	"d,E,s", 	MATCH_CSRRC, MASK_CSRRC,	m_op },
	{ "csrrwi",	"d,E,Z", 	MATCH_CSRRWI, MASK_CSRRWI,	m_op },
	{ "csrrsi",	"d,E,Z", 	MATCH_CSRRSI, MASK_CSRRSI,	m_op },
	{ "csrrci",	"d,E,Z", 	MATCH_CSRRCI, MASK_CSRRCI,	m_op },
	{ "fadd.s",	"D,S,T", 	MATCH_FADD_S, MASK_FADD_S,	m_op },
	{ "fsub.s",	"D,S,T", 	MATCH_FSUB_S, MASK_FSUB_S,	m_op },
	{ "fmul.s",	"D,S,T", 	MATCH_FMUL_S, MASK_FMUL_S,	m_op },
	{ "fdiv.s",	"D,S,T", 	MATCH_FDIV_S, MASK_FDIV_S,	m_op },
	{ "fsgnj.s",	"D,S,T", 	MATCH_FSGNJ_S, MASK_FSGNJ_S,	m_op },
	{ "fsgnjn.s",	"D,S,T", 	MATCH_FSGNJN_S, MASK_FSGNJN_S,	m_op },
	{ "fsgnjx.s",	"D,S,T", 	MATCH_FSGNJX_S, MASK_FSGNJX_S,	m_op },
	{ "fmin.s",	"D,S,T", 	MATCH_FMIN_S, MASK_FMIN_S,	m_op },
	{ "fmax.s",	"D,S,T", 	MATCH_FMAX_S, MASK_FMAX_S,	m_op },
	{ "fsqrt.s",	"D,S", 		MATCH_FSQRT_S, MASK_FSQRT_S,	m_op },
	{ "fadd.d",	"D,S,T", 	MATCH_FADD_D, MASK_FADD_D,	m_op },
	{ "fsub.d",	"D,S,T", 	MATCH_FSUB_D, MASK_FSUB_D,	m_op },
	{ "fmul.d",	"D,S,T", 	MATCH_FMUL_D, MASK_FMUL_D,	m_op },
	{ "fdiv.d",	"D,S,T", 	MATCH_FDIV_D, MASK_FDIV_D,	m_op },
	{ "fsgnj.d",	"D,S,T", 	MATCH_FSGNJ_D, MASK_FSGNJ_D,	m_op },
	{ "fsgnjn.d",	"D,S,T", 	MATCH_FSGNJN_D, MASK_FSGNJN_D,	m_op },
	{ "fsgnjx.d",	"D,S,T", 	MATCH_FSGNJX_D, MASK_FSGNJX_D,	m_op },
	{ "fmin.d",	"D,S,T", 	MATCH_FMIN_D, MASK_FMIN_D,	m_op },
	{ "fmax.d",	"D,S,T", 	MATCH_FMAX_D, MASK_FMAX_D,	m_op },
	{ "fcvt.s.d",	"D,S", 		MATCH_FCVT_S_D, MASK_FCVT_S_D,	m_op },
	{ "fcvt.d.s",	"D,S", 		MATCH_FCVT_D_S, MASK_FCVT_D_S,	m_op },
	{ "fsqrt.d",	"D,S", 		MATCH_FSQRT_D, MASK_FSQRT_D,	m_op },
	{ "fadd.q",	"D,S,T", 	MATCH_FADD_Q, MASK_FADD_Q,	m_op },
	{ "fsub.q",	"D,S,T", 	MATCH_FSUB_Q, MASK_FSUB_Q,	m_op },
	{ "fmul.q",	"D,S,T", 	MATCH_FMUL_Q, MASK_FMUL_Q,	m_op },
	{ "fdiv.q",	"D,S,T", 	MATCH_FDIV_Q, MASK_FDIV_Q,	m_op },
	{ "fsgnj.q",	"D,S,T", 	MATCH_FSGNJ_Q, MASK_FSGNJ_Q,	m_op },
	{ "fsgnjn.q",	"D,S,T", 	MATCH_FSGNJN_Q, MASK_FSGNJN_Q,	m_op },
	{ "fsgnjx.q",	"D,S,T", 	MATCH_FSGNJX_Q, MASK_FSGNJX_Q,	m_op },
	{ "fmin.q",	"D,S,T", 	MATCH_FMIN_Q, MASK_FMIN_Q,	m_op },
	{ "fmax.q",	"D,S,T", 	MATCH_FMAX_Q, MASK_FMAX_Q,	m_op },
	{ "fcvt.s.q",	"D,S", 		MATCH_FCVT_S_Q, MASK_FCVT_S_Q,	m_op },
	{ "fcvt.q.s",	"D,S", 		MATCH_FCVT_Q_S, MASK_FCVT_Q_S,	m_op },
	{ "fcvt.d.q",	"D,S", 		MATCH_FCVT_D_Q, MASK_FCVT_D_Q,	m_op },
	{ "fcvt.q.d",	"D,S", 		MATCH_FCVT_Q_D, MASK_FCVT_Q_D,	m_op },
	{ "fsqrt.q",	"D,S", 		MATCH_FSQRT_Q, MASK_FSQRT_Q,	m_op },
	{ "fle.s",	"d,S,T", 	MATCH_FLE_S, MASK_FLE_S,	m_op },
	{ "flt.s",	"d,S,T", 	MATCH_FLT_S, MASK_FLT_S,	m_op },
	{ "feq.s",	"d,S,T", 	MATCH_FEQ_S, MASK_FEQ_S,	m_op },
	{ "fle.d",	"d,S,T", 	MATCH_FLE_D, MASK_FLE_D,	m_op },
	{ "flt.d",	"d,S,T", 	MATCH_FLT_D, MASK_FLT_D,	m_op },
	{ "feq.d",	"d,S,T", 	MATCH_FEQ_D, MASK_FEQ_D,	m_op },
	{ "fle.q",	"d,S,T", 	MATCH_FLE_Q, MASK_FLE_Q,	m_op },
	{ "flt.q",	"d,S,T", 	MATCH_FLT_Q, MASK_FLT_Q,	m_op },
	{ "feq.q",	"d,S,T", 	MATCH_FEQ_Q, MASK_FEQ_Q,	m_op },
	{ "fcvt.w.s",	"d,S", 		MATCH_FCVT_W_S, MASK_FCVT_W_S,	m_op },
	{ "fcvt.wu.s",	"d,S", 		MATCH_FCVT_WU_S, MASK_FCVT_WU_S,m_op },
	{ "fcvt.l.s",	"d,S", 		MATCH_FCVT_L_S, MASK_FCVT_L_S,	m_op },
	{ "fcvt.lu.s",	"d,S", 		MATCH_FCVT_LU_S, MASK_FCVT_LU_S,m_op },
	{ "fmv.x.w",	"d,S", 		MATCH_FMV_X_W, MASK_FMV_X_W,	m_op },
	{ "fclass.s",	"d,S", 		MATCH_FCLASS_S, MASK_FCLASS_S,	m_op },
	{ "fcvt.w.d",	"d,S", 		MATCH_FCVT_W_D, MASK_FCVT_W_D,	m_op },
	{ "fcvt.wu.d",	"d,S", 		MATCH_FCVT_WU_D, MASK_FCVT_WU_D,m_op },
	{ "fcvt.l.d",	"d,S", 		MATCH_FCVT_L_D, MASK_FCVT_L_D,	m_op },
	{ "fcvt.lu.d",	"d,S", 		MATCH_FCVT_LU_D, MASK_FCVT_LU_D,m_op },
	{ "fmv.x.d",	"d,S", 		MATCH_FMV_X_D, MASK_FMV_X_D,	m_op },
	{ "fclass.d",	"d,S", 		MATCH_FCLASS_D, MASK_FCLASS_D,	m_op },
	{ "fcvt.w.q",	"d,S", 		MATCH_FCVT_W_Q, MASK_FCVT_W_Q,	m_op },
	{ "fcvt.wu.q",	"d,S", 		MATCH_FCVT_WU_Q, MASK_FCVT_WU_Q,m_op },
	{ "fcvt.l.q",	"d,S", 		MATCH_FCVT_L_Q, MASK_FCVT_L_Q,	m_op },
	{ "fcvt.lu.q",	"d,S", 		MATCH_FCVT_LU_Q, MASK_FCVT_LU_Q,m_op },
	{ "fmv.x.q",	"d,S", 		MATCH_FMV_X_Q, MASK_FMV_X_Q,	m_op },
	{ "fclass.q",	"d,S", 		MATCH_FCLASS_Q, MASK_FCLASS_Q,	m_op },
	{ "fcvt.s.w",	"D,s", 		MATCH_FCVT_S_W, MASK_FCVT_S_W,	m_op },
	{ "fcvt.s.wu",	"D,s", 		MATCH_FCVT_S_WU, MASK_FCVT_S_WU,m_op },
	{ "fcvt.s.l",	"D,s", 		MATCH_FCVT_S_L, MASK_FCVT_S_L,	m_op },
	{ "fcvt.s.lu",	"D,s", 		MATCH_FCVT_S_LU, MASK_FCVT_S_LU,m_op },
	{ "fmv.w.x",	"D,s", 		MATCH_FMV_W_X, MASK_FMV_W_X,	m_op },
	{ "fcvt.d.w",	"D,s", 		MATCH_FCVT_D_W, MASK_FCVT_D_W,	m_op },
	{ "fcvt.d.wu",	"D,s", 		MATCH_FCVT_D_WU, MASK_FCVT_D_WU,m_op },
	{ "fcvt.d.l",	"D,s", 		MATCH_FCVT_D_L, MASK_FCVT_D_L,	m_op },
	{ "fcvt.d.lu",	"D,s", 		MATCH_FCVT_D_LU, MASK_FCVT_D_LU,m_op },
	{ "fmv.d.x",	"D,s", 		MATCH_FMV_D_X, MASK_FMV_D_X,	m_op },
	{ "fcvt.q.w",	"D,s", 		MATCH_FCVT_Q_W, MASK_FCVT_Q_W,	m_op },
	{ "fcvt.q.wu",	"D,s", 		MATCH_FCVT_Q_WU, MASK_FCVT_Q_WU,m_op },
	{ "fcvt.q.l",	"D,s", 		MATCH_FCVT_Q_L, MASK_FCVT_Q_L,	m_op },
	{ "fcvt.q.lu",	"D,s", 		MATCH_FCVT_Q_LU, MASK_FCVT_Q_LU,m_op },
	{ "fmv.q.x",	"D,s", 		MATCH_FMV_Q_X, MASK_FMV_Q_X,	m_op },
	{ "flw",	"D,o(s)", 	MATCH_FLW, MASK_FLW,		m_op },
	{ "fld",	"D,o(s)", 	MATCH_FLD, MASK_FLD,		m_op },
	{ "flq",	"D,o(s)", 	MATCH_FLQ, MASK_FLQ,		m_op },
	{ "fsw",	"T,q(s)", 	MATCH_FSW, MASK_FSW,		m_op },
	{ "fsd",	"T,q(s)", 	MATCH_FSD, MASK_FSD,		m_op },
	{ "fsq",	"T,q(s)", 	MATCH_FSQ, MASK_FSQ,		m_op },
	{ "fmadd.s",	"D,S,T,R", 	MATCH_FMADD_S, MASK_FMADD_S,	m_op },
	{ "fmsub.s",	"D,S,T,R", 	MATCH_FMSUB_S, MASK_FMSUB_S,	m_op },
	{ "fnmsub.s",	"D,S,T,R", 	MATCH_FNMSUB_S, MASK_FNMSUB_S,	m_op },
	{ "fnmadd.s",	"D,S,T,R", 	MATCH_FNMADD_S, MASK_FNMADD_S,	m_op },
	{ "fmadd.d",	"D,S,T,R", 	MATCH_FMADD_D, MASK_FMADD_D,	m_op },
	{ "fmsub.d",	"D,S,T,R", 	MATCH_FMSUB_D, MASK_FMSUB_D,	m_op },
	{ "fnmsub.d",	"D,S,T,R", 	MATCH_FNMSUB_D, MASK_FNMSUB_D,	m_op },
	{ "fnmadd.d",	"D,S,T,R", 	MATCH_FNMADD_D, MASK_FNMADD_D,	m_op },
	{ "fmadd.q",	"D,S,T,R", 	MATCH_FMADD_Q, MASK_FMADD_Q,	m_op },
	{ "fmsub.q",	"D,S,T,R", 	MATCH_FMSUB_Q, MASK_FMSUB_Q,	m_op },
	{ "fnmsub.q",	"D,S,T,R", 	MATCH_FNMSUB_Q, MASK_FNMSUB_Q,	m_op },
	{ "fnmadd.q",	"D,S,T,R", 	MATCH_FNMADD_Q, MASK_FNMADD_Q,	m_op },
	{ NULL, NULL, 0, 0, NULL },
};

static struct riscv_op riscv_c_opcodes[] = {
	/* Aliases first */
	{ "ret","",MATCH_C_JR | (X_RA << RD_SHIFT), MASK_C_JR | RD_MASK, m_op},

	/* C-Compressed ISA Extension Instructions */
	{ "c.nop",	"", 		MATCH_C_NOP, MASK_C_NOP,	m_op },
	{ "c.ebreak",	"", 		MATCH_C_EBREAK, MASK_C_EBREAK,	m_op },
	{ "c.jr",	"d", 		MATCH_C_JR, MASK_C_JR,		m_op },
	{ "c.jalr",	"d", 		MATCH_C_JALR, MASK_C_JALR,	m_op },
	{ "c.jal",	"Ca", 		MATCH_C_JAL, MASK_C_JAL,	m_op },
	{ "c.ld",	"Ct,Cl(Cs)", 	MATCH_C_LD, MASK_C_LD,		m_op },
	{ "c.sd",	"Ct,Cl(Cs)", 	MATCH_C_SD, MASK_C_SD,		m_op },
	{ "c.addiw",	"d,Co", 	MATCH_C_ADDIW, MASK_C_ADDIW,	m_op },
	{ "c.ldsp",	"d,Cn(Cc)", 	MATCH_C_LDSP, MASK_C_LDSP,	m_op },
	{ "c.sdsp",	"CV,CN(Cc)", 	MATCH_C_SDSP, MASK_C_SDSP,	m_op },
	{ "c.addi4spn",	"", 	MATCH_C_ADDI4SPN, MASK_C_ADDI4SPN,	m_op },
	{ "c.addi16sp",	"", 	MATCH_C_ADDI16SP, MASK_C_ADDI16SP,	m_op },
	{ "c.fld",	"CD,Cl(Cs)", 	MATCH_C_FLD, MASK_C_FLD,	m_op },
	{ "c.lw",	"Ct,Ck(Cs)", 	MATCH_C_LW, MASK_C_LW,		m_op },
	{ "c.flw",	"CD,Ck(Cs)", 	MATCH_C_FLW, MASK_C_FLW,	m_op },
	{ "c.fsd",	"CD,Cl(Cs)", 	MATCH_C_FSD, MASK_C_FSD,	m_op },
	{ "c.sw",	"Ct,Ck(Cs)", 	MATCH_C_SW, MASK_C_SW,		m_op },
	{ "c.fsw",	"CD,Ck(Cs)", 	MATCH_C_FSW, MASK_C_FSW,	m_op },
	{ "c.addi",	"d,Co", 	MATCH_C_ADDI, MASK_C_ADDI,	m_op },
	{ "c.li",	"d,Co", 	MATCH_C_LI, MASK_C_LI,		m_op },
	{ "c.lui",	"d,Cu", 	MATCH_C_LUI, MASK_C_LUI,	m_op },
	{ "c.srli",	"Cs,C>", 	MATCH_C_SRLI, MASK_C_SRLI,	m_op },
	{ "c.srai",	"Cs,C>", 	MATCH_C_SRAI, MASK_C_SRAI,	m_op },
	{ "c.andi",	"Cs,Co", 	MATCH_C_ANDI, MASK_C_ANDI,	m_op },
	{ "c.sub",	"Cs,Ct", 	MATCH_C_SUB, MASK_C_SUB,	m_op },
	{ "c.xor",	"Cs,Ct", 	MATCH_C_XOR, MASK_C_XOR,	m_op },
	{ "c.or",	"Cs,Ct", 	MATCH_C_OR, MASK_C_OR,		m_op },
	{ "c.and",	"Cs,Ct", 	MATCH_C_AND, MASK_C_AND,	m_op },
	{ "c.subw",	"Cs,Ct", 	MATCH_C_SUBW, MASK_C_SUBW,	m_op },
	{ "c.addw",	"Cs,Ct", 	MATCH_C_ADDW, MASK_C_ADDW,	m_op },
	{ "c.j",	"Ca", 		MATCH_C_J, MASK_C_J,		m_op },
	{ "c.beqz",	"Cs,Cp", 	MATCH_C_BEQZ, MASK_C_BEQZ,	m_op },
	{ "c.bnez",	"Cs,Cp", 	MATCH_C_BNEZ, MASK_C_BNEZ,	m_op },
	{ "c.slli",	"d,C>", 	MATCH_C_SLLI, MASK_C_SLLI,	m_op },
	{ "c.fldsp",	"D,Cn(Cc)", 	MATCH_C_FLDSP, MASK_C_FLDSP,	m_op },
	{ "c.lwsp",	"d,Cm(Cc)", 	MATCH_C_LWSP, MASK_C_LWSP,	m_op },
	{ "c.flwsp",	"D,Cm(Cc)", 	MATCH_C_FLWSP, MASK_C_FLWSP,	m_op },
	{ "c.mv",	"d,CV", 	MATCH_C_MV, MASK_C_MV,		m_op },
	{ "c.add",	"d,CV", 	MATCH_C_ADD, MASK_C_ADD,	m_op },
	{ "c.fsdsp",	"CT,CN(Cc)", 	MATCH_C_FSDSP, MASK_C_FSDSP,	m_op },
	{ "c.swsp",	"CV,CM(Cc)", 	MATCH_C_SWSP, MASK_C_SWSP,	m_op },
	{ "c.fswsp",	"CT,CM(Cc)", 	MATCH_C_FSWSP, MASK_C_FSWSP,	m_op },
	{ NULL, NULL, 0, 0, NULL },
};

static int
oprint(struct riscv_op *op, vm_offset_t loc, int insn)
{
	uint32_t rd, rs1, rs2, rs3;
	uint32_t val;
	const char *csr_name;
	int imm;
	char *p;

	p = op->fmt;

	rd = (insn & RD_MASK) >> RD_SHIFT;
	rs1 = (insn & RS1_MASK) >> RS1_SHIFT;
	rs2 = (insn & RS2_MASK) >> RS2_SHIFT;

	db_printf("%s\t", op->name);

	while (*p) {
		switch (*p) {
		case 'C':	/* C-Compressed ISA extension */
			switch (*++p) {
			case 't':
				rd = (insn >> 2) & 0x7;
				rd += 0x8;
				db_printf("%s", reg_name[rd]);
				break;
			case 's':
				rs2 = (insn >> 7) & 0x7;
				rs2 += 0x8;
				db_printf("%s", reg_name[rs2]);
				break;
			case 'l':
				imm = ((insn >> 10) & 0x7) << 3;
				imm |= ((insn >> 5) & 0x3) << 6;
				if (imm & (1 << 8))
					imm |= 0xffffff << 8;
				db_printf("%d", imm);
				break;
			case 'k':
				imm = ((insn >> 10) & 0x7) << 3;
				imm |= ((insn >> 6) & 0x1) << 2;
				imm |= ((insn >> 5) & 0x1) << 6;
				if (imm & (1 << 8))
					imm |= 0xffffff << 8;
				db_printf("%d", imm);
				break;
			case 'c':
				db_printf("sp");
				break;
			case 'n':
				imm = ((insn >> 5) & 0x3) << 3;
				imm |= ((insn >> 12) & 0x1) << 5;
				imm |= ((insn >> 2) & 0x7) << 6;
				if (imm & (1 << 8))
					imm |= 0xffffff << 8;
				db_printf("%d", imm);
				break;
			case 'N':
				imm = ((insn >> 10) & 0x7) << 3;
				imm |= ((insn >> 7) & 0x7) << 6;
				if (imm & (1 << 8))
					imm |= 0xffffff << 8;
				db_printf("%d", imm);
				break;
			case 'u':
				imm = ((insn >> 2) & 0x1f) << 0;
				imm |= ((insn >> 12) & 0x1) << 5;
				if (imm & (1 << 5))
					imm |= (0x7ffffff << 5); /* sign ext */
				db_printf("0x%lx", imm);
				break;
			case 'o':
				imm = ((insn >> 2) & 0x1f) << 0;
				imm |= ((insn >> 12) & 0x1) << 5;
				if (imm & (1 << 5))
					imm |= (0x7ffffff << 5); /* sign ext */
				db_printf("%d", imm);
				break;
			case 'a':
				/* imm[11|4|9:8|10|6|7|3:1|5] << 2 */
				imm = ((insn >> 3) & 0x7) << 1;
				imm |= ((insn >> 11) & 0x1) << 4;
				imm |= ((insn >> 2) & 0x1) << 5;
				imm |= ((insn >> 7) & 0x1) << 6;
				imm |= ((insn >> 6) & 0x1) << 7;
				imm |= ((insn >> 9) & 0x3) << 8;
				imm |= ((insn >> 8) & 0x1) << 10;
				imm |= ((insn >> 12) & 0x1) << 11;
				if (imm & (1 << 11))
					imm |= (0xfffff << 12);	/* sign ext */
				db_printf("0x%lx", (loc + imm));
				break;
			case 'V':
				rs2 = (insn >> 2) & 0x1f;
				db_printf("%s", reg_name[rs2]);
				break;
			case '>':
				imm = ((insn >> 2) & 0x1f) << 0;
				imm |= ((insn >> 12) & 0x1) << 5;
				db_printf("%d", imm);
			};
			break;
		case 'd':
			db_printf("%s", reg_name[rd]);
			break;
		case 'D':
			db_printf("%s", fp_reg_name[rd]);
			break;
		case 's':
			db_printf("%s", reg_name[rs1]);
			break;
		case 'S':
			db_printf("%s", fp_reg_name[rs1]);
			break;
		case 't':
			db_printf("%s", reg_name[rs2]);
			break;
		case 'T':
			db_printf("%s", fp_reg_name[rs2]);
			break;
		case 'R':
			rs3 = (insn >> 27) & 0x1f;
			db_printf("%s", fp_reg_name[rs3]);
			break;
		case 'Z':
			imm = (insn >> 15) & 0x1f;
			db_printf("%d", imm);
			break;
		case 'p':
			imm = ((insn >> 8) & 0xf) << 1;
			imm |= ((insn >> 25) & 0x3f) << 5;
			imm |= ((insn >> 7) & 0x1) << 11;
			imm |= ((insn >> 31) & 0x1) << 12;
			if (imm & (1 << 12))
				imm |= (0xfffff << 12);	/* sign extend */
			db_printf("0x%016lx", (loc + imm));
			break;
		case '(':
		case ')':
		case '[':
		case ']':
		case ',':
			db_printf("%c", *p);
			break;
		case '0':
			if (!p[1])
				db_printf("%c", *p);
			break;
			
		case 'o':
			imm = (insn >> 20) & 0xfff;
			if (imm & (1 << 11))
				imm |= (0xfffff << 12);	/* sign extend */
			db_printf("%d", imm);
			break;
		case 'q':
			imm = (insn >> 7) & 0x1f;
			imm |= ((insn >> 25) & 0x7f) << 5;
			if (imm & (1 << 11))
				imm |= (0xfffff << 12);	/* sign extend */
			db_printf("%d", imm);
			break;
		case 'a':
			/* imm[20|10:1|11|19:12] << 12 */
			imm = ((insn >> 21) & 0x3ff) << 1;
			imm |= ((insn >> 20) & 0x1) << 11;
			imm |= ((insn >> 12) & 0xff) << 12;
			imm |= ((insn >> 31) & 0x1) << 20;
			if (imm & (1 << 20))
				imm |= (0xfff << 20);	/* sign extend */
			db_printf("0x%lx", (loc + imm));
			break;
		case 'u':
			/* imm[31:12] << 12 */
			imm = (insn >> 12) & 0xfffff;
			if (imm & (1 << 20))
				imm |= (0xfff << 20);	/* sign extend */
			db_printf("0x%lx", imm);
			break;
		case 'j':
			/* imm[11:0] << 20 */
			imm = (insn >> 20) & 0xfff;
			if (imm & (1 << 11))
				imm |= (0xfffff << 12); /* sign extend */
			db_printf("%d", imm);
			break;
		case '>':
			val = (insn >> 20) & 0x3f;
			db_printf("0x%x", val);
			break;
		case '<':
			val = (insn >> 20) & 0x1f;
			db_printf("0x%x", val);
			break;
		case 'E':
			val = (insn >> 20) & 0xfff;
			csr_name = NULL;
			switch (val) {
#define DECLARE_CSR(name, num) case num: csr_name = #name; break;
#include "machine/encoding.h"
#undef DECLARE_CSR
			}
			if (csr_name)
				db_printf("%s", csr_name);
			else
				db_printf("0x%x", val);
			break;
		case 'P':
			if (insn & (1 << 27)) db_printf("i");
			if (insn & (1 << 26)) db_printf("o");
			if (insn & (1 << 25)) db_printf("r");
			if (insn & (1 << 24)) db_printf("w");
			break;
		case 'Q':
			if (insn & (1 << 23)) db_printf("i");
			if (insn & (1 << 22)) db_printf("o");
			if (insn & (1 << 21)) db_printf("r");
			if (insn & (1 << 20)) db_printf("w");
			break;
		}

		p++;
	}

	return (0);
}

vm_offset_t
db_disasm(vm_offset_t loc, bool altfmt)
{
	struct riscv_op *op;
	uint32_t insn;
	int j;

	insn = db_get_value(loc, 4, 0);
	for (j = 0; riscv_opcodes[j].name != NULL; j++) {
		op = &riscv_opcodes[j];
		if (op->match_func(op, insn)) {
			oprint(op, loc, insn);
			return(loc + 4);
		}
	};

	insn = db_get_value(loc, 2, 0);
	for (j = 0; riscv_c_opcodes[j].name != NULL; j++) {
		op = &riscv_c_opcodes[j];
		if (op->match_func(op, insn)) {
			oprint(op, loc, insn);
			break;
		}
	};

	return(loc + 2);
}
