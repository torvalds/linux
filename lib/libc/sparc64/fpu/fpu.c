/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 */
/*-
 * Copyright 2001 by Thomas Moestl <tmm@FreeBSD.org>.  All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	@(#)fpu.c	8.1 (Berkeley) 6/11/93
 *	$NetBSD: fpu.c,v 1.11 2000/12/06 01:47:50 mrg Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include "namespace.h"
#include <errno.h>
#include <signal.h>
#ifdef FPU_DEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include "un-namespace.h"
#include "libc_private.h"

#include <machine/fp.h>
#include <machine/frame.h>
#include <machine/fsr.h>
#include <machine/instr.h>
#include <machine/pcb.h>
#include <machine/tstate.h>

#include "__sparc_utrap_private.h"
#include "fpu_emu.h"
#include "fpu_extern.h"

/*
 * Translate current exceptions into `first' exception.  The
 * bits go the wrong way for ffs() (0x10 is most important, etc).
 * There are only 5, so do it the obvious way.
 */
#define	X1(x) x
#define	X2(x) x,x
#define	X4(x) x,x,x,x
#define	X8(x) X4(x),X4(x)
#define	X16(x) X8(x),X8(x)

static const char cx_to_trapx[] = {
	X1(FSR_NX),
	X2(FSR_DZ),
	X4(FSR_UF),
	X8(FSR_OF),
	X16(FSR_NV)
};

#ifdef FPU_DEBUG
#ifdef FPU_DEBUG_MASK
int __fpe_debug = FPU_DEBUG_MASK;
#else
int __fpe_debug = 0;
#endif
#endif	/* FPU_DEBUG */

static int __fpu_execute(struct utrapframe *, struct fpemu *, u_int32_t,
    u_long);

/*
 * Need to use an fpstate on the stack; we could switch, so we cannot safely
 * modify the pcb one, it might get overwritten.
 */
int
__fpu_exception(struct utrapframe *uf)
{
	struct fpemu fe;
	u_long fsr, tstate;
	u_int insn;
	int sig;

	fsr = uf->uf_fsr;

	switch (FSR_GET_FTT(fsr)) {
	case FSR_FTT_NONE:
		__utrap_write("lost FPU trap type\n");
		return (0);
	case FSR_FTT_IEEE:
		return (SIGFPE);
	case FSR_FTT_SEQERR:
		__utrap_write("FPU sequence error\n");
		return (SIGFPE);
	case FSR_FTT_HWERR:
		__utrap_write("FPU hardware error\n");
		return (SIGFPE);
	case FSR_FTT_UNFIN:
	case FSR_FTT_UNIMP:
		break;
	default:
		__utrap_write("unknown FPU error\n");
		return (SIGFPE);
	}

	fe.fe_fsr = fsr & ~FSR_FTT_MASK;
	insn = *(u_int32_t *)uf->uf_pc;
	if (IF_OP(insn) != IOP_MISC || (IF_F3_OP3(insn) != INS2_FPop1 &&
	    IF_F3_OP3(insn) != INS2_FPop2))
		__utrap_panic("bogus FP fault");
	tstate = uf->uf_state;
	sig = __fpu_execute(uf, &fe, insn, tstate);
	if (sig != 0)
		return (sig);
	__asm __volatile("ldx %0, %%fsr" : : "m" (fe.fe_fsr));
	return (0);
}

#ifdef FPU_DEBUG
/*
 * Dump a `fpn' structure.
 */
void
__fpu_dumpfpn(struct fpn *fp)
{
	static const char *const class[] = {
		"SNAN", "QNAN", "ZERO", "NUM", "INF"
	};

	printf("%s %c.%x %x %x %xE%d", class[fp->fp_class + 2],
		fp->fp_sign ? '-' : ' ',
		fp->fp_mant[0],	fp->fp_mant[1],
		fp->fp_mant[2], fp->fp_mant[3],
		fp->fp_exp);
}
#endif

static const int opmask[] = {0, 0, 1, 3, 1};

/* Decode 5 bit register field depending on the type. */
#define	RN_DECODE(tp, rn) \
	((tp) >= FTYPE_DBL ? INSFPdq_RN(rn) & ~opmask[tp] : (rn))

/*
 * Helper for forming the below case statements. Build only the op3 and opf
 * field of the instruction, these are the only ones that need to match.
 */
#define	FOP(op3, opf) \
	((op3) << IF_F3_OP3_SHIFT | (opf) << IF_F3_OPF_SHIFT)

/*
 * Implement a move operation for all supported operand types. The additional
 * nand and xor parameters will be applied to the upper 32 bit word of the
 * source operand. This allows to implement fabs and fneg (for fp operands
 * only!) using this functions, too, by passing (1U << 31) for one of the
 * parameters, and 0 for the other.
 */
static void
__fpu_mov(struct fpemu *fe, int type, int rd, int rs2, u_int32_t nand,
    u_int32_t xor)
{

	if (type == FTYPE_INT || type == FTYPE_SNG)
		__fpu_setreg(rd, (__fpu_getreg(rs2) & ~nand) ^ xor);
	else {
		/*
		 * Need to use the double versions to be able to access
		 * the upper 32 fp registers.
		 */
		__fpu_setreg64(rd, (__fpu_getreg64(rs2) &
		    ~((u_int64_t)nand << 32)) ^ ((u_int64_t)xor << 32));
		if (type == FTYPE_EXT)
			__fpu_setreg64(rd + 2, __fpu_getreg64(rs2 + 2));
	}
}

static __inline void
__fpu_ccmov(struct fpemu *fe, int type, int rd, int rs2,
    u_int32_t insn, int fcc)
{

	if (IF_F4_COND(insn) == fcc)
		__fpu_mov(fe, type, rd, rs2, 0, 0);
}

static int
__fpu_cmpck(struct fpemu *fe)
{
	u_long fsr;
	int cx;

	/*
	 * The only possible exception here is NV; catch it
	 * early and get out, as there is no result register.
	 */
	cx = fe->fe_cx;
	fsr = fe->fe_fsr | (cx << FSR_CEXC_SHIFT);
	if (cx != 0) {
		if (fsr & (FSR_NV << FSR_TEM_SHIFT)) {
			fe->fe_fsr = (fsr & ~FSR_FTT_MASK) |
			    FSR_FTT(FSR_FTT_IEEE);
			return (SIGFPE);
		}
		fsr |= FSR_NV << FSR_AEXC_SHIFT;
	}
	fe->fe_fsr = fsr;
	return (0);
}

/*
 * Execute an FPU instruction (one that runs entirely in the FPU; not
 * FBfcc or STF, for instance).  On return, fe->fe_fs->fs_fsr will be
 * modified to reflect the setting the hardware would have left.
 *
 * Note that we do not catch all illegal opcodes, so you can, for instance,
 * multiply two integers this way.
 */
static int
__fpu_execute(struct utrapframe *uf, struct fpemu *fe, u_int32_t insn,
    u_long tstate)
{
	struct fpn *fp;
	int opf, rs1, rs2, rd, type, mask, cx, cond __unused;
	u_long reg, fsr;
	u_int space[4];

	/*
	 * `Decode' and execute instruction.  Start with no exceptions.
	 * The type of almost any OPF opcode is in the bottom two bits, so we
	 * squish them out here.
	 */
	opf = insn & (IF_MASK(IF_F3_OP3_SHIFT, IF_F3_OP3_BITS) |
	    IF_MASK(IF_F3_OPF_SHIFT + 2, IF_F3_OPF_BITS - 2));
	type = IF_F3_OPF(insn) & 3;
	rs1 = RN_DECODE(type, IF_F3_RS1(insn));
	rs2 = RN_DECODE(type, IF_F3_RS2(insn));
	rd = RN_DECODE(type, IF_F3_RD(insn));
	cond = 0;
#ifdef notdef
	if ((rs1 | rs2 | rd) & opmask[type])
		return (SIGILL);
#endif
	fsr = fe->fe_fsr;
	fe->fe_fsr &= ~FSR_CEXC_MASK;
	fe->fe_cx = 0;
	switch (opf) {
	case FOP(INS2_FPop2, INSFP2_FMOV_CC(IFCC_FCC(0))):
		__fpu_ccmov(fe, type, rd, rs2, insn, FSR_GET_FCC0(fsr));
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_CC(IFCC_FCC(1))):
		__fpu_ccmov(fe, type, rd, rs2, insn, FSR_GET_FCC1(fsr));
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_CC(IFCC_FCC(2))):
		__fpu_ccmov(fe, type, rd, rs2, insn, FSR_GET_FCC2(fsr));
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_CC(IFCC_FCC(3))):
		__fpu_ccmov(fe, type, rd, rs2, insn, FSR_GET_FCC3(fsr));
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_CC(IFCC_ICC)):
		__fpu_ccmov(fe, type, rd, rs2, insn,
		    (tstate & TSTATE_ICC_MASK) >> TSTATE_ICC_SHIFT);
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_CC(IFCC_XCC)):
		__fpu_ccmov(fe, type, rd, rs2, insn,
		    (tstate & TSTATE_XCC_MASK) >> (TSTATE_XCC_SHIFT));
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_RC(IRCOND_Z)):
		reg = __emul_fetch_reg(uf, IF_F4_RS1(insn));
		if (reg == 0)
			__fpu_mov(fe, type, rd, rs2, 0, 0);
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_RC(IRCOND_LEZ)):
		reg = __emul_fetch_reg(uf, IF_F4_RS1(insn));
		if (reg <= 0)
			__fpu_mov(fe, type, rd, rs2, 0, 0);
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_RC(IRCOND_LZ)):
		reg = __emul_fetch_reg(uf, IF_F4_RS1(insn));
		if (reg < 0)
			__fpu_mov(fe, type, rd, rs2, 0, 0);
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_RC(IRCOND_NZ)):
		reg = __emul_fetch_reg(uf, IF_F4_RS1(insn));
		if (reg != 0)
			__fpu_mov(fe, type, rd, rs2, 0, 0);
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_RC(IRCOND_GZ)):
		reg = __emul_fetch_reg(uf, IF_F4_RS1(insn));
		if (reg > 0)
			__fpu_mov(fe, type, rd, rs2, 0, 0);
		return (0);
	case FOP(INS2_FPop2, INSFP2_FMOV_RC(IRCOND_GEZ)):
		reg = __emul_fetch_reg(uf, IF_F4_RS1(insn));
		if (reg >= 0)
			__fpu_mov(fe, type, rd, rs2, 0, 0);
		return (0);
	case FOP(INS2_FPop2, INSFP2_FCMP):
		__fpu_explode(fe, &fe->fe_f1, type, rs1);
		__fpu_explode(fe, &fe->fe_f2, type, rs2);
		__fpu_compare(fe, 0, IF_F3_CC(insn));
		return (__fpu_cmpck(fe));
	case FOP(INS2_FPop2, INSFP2_FCMPE):
		__fpu_explode(fe, &fe->fe_f1, type, rs1);
		__fpu_explode(fe, &fe->fe_f2, type, rs2);
		__fpu_compare(fe, 1, IF_F3_CC(insn));
		return (__fpu_cmpck(fe));
	case FOP(INS2_FPop1, INSFP1_FMOV):
		__fpu_mov(fe, type, rd, rs2, 0, 0);
		return (0);
	case FOP(INS2_FPop1, INSFP1_FNEG):
		__fpu_mov(fe, type, rd, rs2, 0, (1U << 31));
		return (0);
	case FOP(INS2_FPop1, INSFP1_FABS):
		__fpu_mov(fe, type, rd, rs2, (1U << 31), 0);
		return (0);
	case FOP(INS2_FPop1, INSFP1_FSQRT):
		__fpu_explode(fe, &fe->fe_f1, type, rs2);
		fp = __fpu_sqrt(fe);
		break;
	case FOP(INS2_FPop1, INSFP1_FADD):
		__fpu_explode(fe, &fe->fe_f1, type, rs1);
		__fpu_explode(fe, &fe->fe_f2, type, rs2);
		fp = __fpu_add(fe);
		break;
	case FOP(INS2_FPop1, INSFP1_FSUB):
		__fpu_explode(fe, &fe->fe_f1, type, rs1);
		__fpu_explode(fe, &fe->fe_f2, type, rs2);
		fp = __fpu_sub(fe);
		break;
	case FOP(INS2_FPop1, INSFP1_FMUL):
		__fpu_explode(fe, &fe->fe_f1, type, rs1);
		__fpu_explode(fe, &fe->fe_f2, type, rs2);
		fp = __fpu_mul(fe);
		break;
	case FOP(INS2_FPop1, INSFP1_FDIV):
		__fpu_explode(fe, &fe->fe_f1, type, rs1);
		__fpu_explode(fe, &fe->fe_f2, type, rs2);
		fp = __fpu_div(fe);
		break;
	case FOP(INS2_FPop1, INSFP1_FsMULd):
	case FOP(INS2_FPop1, INSFP1_FdMULq):
		if (type == FTYPE_EXT)
			return (SIGILL);
		__fpu_explode(fe, &fe->fe_f1, type, rs1);
		__fpu_explode(fe, &fe->fe_f2, type, rs2);
		type++;	/* single to double, or double to quad */
		/*
		 * Recalculate rd (the old type applied for the source regs
		 * only, the target one has a different size).
		 */
		rd = RN_DECODE(type, IF_F3_RD(insn));
		fp = __fpu_mul(fe);
		break;
	case FOP(INS2_FPop1, INSFP1_FxTOs):
	case FOP(INS2_FPop1, INSFP1_FxTOd):
	case FOP(INS2_FPop1, INSFP1_FxTOq):
		type = FTYPE_LNG;
		rs2 = RN_DECODE(type, IF_F3_RS2(insn));
		__fpu_explode(fe, fp = &fe->fe_f1, type, rs2);
		/* sneaky; depends on instruction encoding */
		type = (IF_F3_OPF(insn) >> 2) & 3;
		rd = RN_DECODE(type, IF_F3_RD(insn));
		break;
	case FOP(INS2_FPop1, INSFP1_FTOx):
		__fpu_explode(fe, fp = &fe->fe_f1, type, rs2);
		type = FTYPE_LNG;
		rd = RN_DECODE(type, IF_F3_RD(insn));
		break;
	case FOP(INS2_FPop1, INSFP1_FTOs):
	case FOP(INS2_FPop1, INSFP1_FTOd):
	case FOP(INS2_FPop1, INSFP1_FTOq):
	case FOP(INS2_FPop1, INSFP1_FTOi):
		__fpu_explode(fe, fp = &fe->fe_f1, type, rs2);
		/* sneaky; depends on instruction encoding */
		type = (IF_F3_OPF(insn) >> 2) & 3;
		rd = RN_DECODE(type, IF_F3_RD(insn));
		break;
	default:
		return (SIGILL);
	}

	/*
	 * ALU operation is complete.  Collapse the result and then check
	 * for exceptions.  If we got any, and they are enabled, do not
	 * alter the destination register, just stop with an exception.
	 * Otherwise set new current exceptions and accrue.
	 */
	__fpu_implode(fe, fp, type, space);
	cx = fe->fe_cx;
	if (cx != 0) {
		mask = (fsr >> FSR_TEM_SHIFT) & FSR_TEM_MASK;
		if (cx & mask) {
			/* not accrued??? */
			fsr = (fsr & ~FSR_FTT_MASK) |
			    FSR_FTT(FSR_FTT_IEEE) |
			    FSR_CEXC(cx_to_trapx[(cx & mask) - 1]);
			return (SIGFPE);
		}
		fsr |= (cx << FSR_CEXC_SHIFT) | (cx << FSR_AEXC_SHIFT);
	}
	fe->fe_fsr = fsr;
	if (type == FTYPE_INT || type == FTYPE_SNG)
		__fpu_setreg(rd, space[0]);
	else {
		__fpu_setreg64(rd, ((u_int64_t)space[0] << 32) | space[1]);
		if (type == FTYPE_EXT)
			__fpu_setreg64(rd + 2,
			    ((u_int64_t)space[2] << 32) | space[3]);
	}
	return (0);	/* success */
}
