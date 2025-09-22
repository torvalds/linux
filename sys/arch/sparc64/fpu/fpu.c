/*	$OpenBSD: fpu.c,v 1.26 2024/03/29 21:14:31 miod Exp $	*/
/*	$NetBSD: fpu.c,v 1.11 2000/12/06 01:47:50 mrg Exp $ */

/*
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
 *	@(#)fpu.c	8.1 (Berkeley) 6/11/93
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/signalvar.h>

#include <machine/instr.h>
#include <machine/fsr.h>
#include <machine/reg.h>

#include <sparc64/fpu/fpu_emu.h>
#include <sparc64/fpu/fpu_extern.h>

int fpu_regoffset(int, int);
int fpu_insn_fmov(struct fpstate *, struct fpemu *, union instr);
int fpu_insn_fabs(struct fpstate *, struct fpemu *, union instr);
int fpu_insn_fneg(struct fpstate *, struct fpemu *, union instr);
int fpu_insn_itof(struct fpemu *, union instr, int, int *,
    int *, u_int *);
int fpu_insn_ftoi(struct fpemu *, union instr, int *, int, u_int *);
int fpu_insn_ftof(struct fpemu *, union instr, int *, int *, u_int *);
int fpu_insn_fsqrt(struct fpemu *, union instr, int *, int *, u_int *);
int fpu_insn_fcmp(struct fpstate *, struct fpemu *, union instr, int);
int fpu_insn_fmul(struct fpemu *, union instr, int *, int *, u_int *);
int fpu_insn_fmulx(struct fpemu *, union instr, int *, int *, u_int *);
int fpu_insn_fdiv(struct fpemu *, union instr, int *, int *, u_int *);
int fpu_insn_fadd(struct fpemu *, union instr, int *, int *, u_int *);
int fpu_insn_fsub(struct fpemu *, union instr, int *, int *, u_int *);
int fpu_insn_fmovcc(struct proc *, struct fpstate *, union instr);
int fpu_insn_fmovr(struct proc *, struct fpstate *, union instr);
void fpu_fcopy(u_int *, u_int *, int);

#ifdef DEBUG
int fpe_debug = 0;

/*
 * Dump a `fpn' structure.
 */
void
fpu_dumpfpn(struct fpn *fp)
{
	static char *class[] = { "SNAN", "QNAN", "ZERO", "NUM", "INF" };

	printf("%s %c.%x %x %x %xE%d", class[fp->fp_class + 2],
	    fp->fp_sign ? '-' : ' ', fp->fp_mant[0], fp->fp_mant[1],
	    fp->fp_mant[2], fp->fp_mant[3], fp->fp_exp);
}
void
fpu_dumpstate(struct fpstate *fs)
{
	int i;

	for (i = 0; i < 64; i++)
		printf("%%f%02d: %08x%s",
		    i, fs->fs_regs[i], ((i & 3) == 3) ? "\n" : "   ");
}
#endif

/*
 * fpu_execute returns the following error numbers (0 = no error):
 */
#define	FPE		1	/* take a floating point exception */
#define	NOTFPU		2	/* not an FPU instruction */

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
static const u_char fpu_codes[] = {
	X1(FPE_FLTINEX_TRAP),
	X2(FPE_FLTDIV_TRAP),
	X4(FPE_FLTUND_TRAP),
	X8(FPE_FLTOVF_TRAP),
	X16(FPE_FLTOPERR_TRAP)
};

static const int fpu_types[] = {
	X1(FPE_FLTRES),
	X2(FPE_FLTDIV),
	X4(FPE_FLTUND),
	X8(FPE_FLTOVF),
	X16(FPE_FLTINV)
};

void
fpu_fcopy(u_int *src, u_int *dst, int type)
{
	*dst++ = *src++;
	if (type == FTYPE_SNG || type == FTYPE_INT)
		return;
	*dst++ = *src++;
	if (type != FTYPE_EXT)
		return;
	*dst++ = *src++;
	*dst = *src;
}

/*
 * The FPU gave us an exception.  Clean up the mess.
 */
void
fpu_cleanup(struct proc *p, struct fpstate *fs, union instr instr,
    union sigval sv)
{
	int i, fsr = fs->fs_fsr, error;
	struct fpemu fe;

	switch ((fsr >> FSR_FTT_SHIFT) & FSR_FTT_MASK) {
	case FSR_TT_NONE:
#ifdef DEBUG
		printf("fpu_cleanup: invoked although no exception\n");
#endif
		return;
	case FSR_TT_IEEE:
		if ((i = fsr & FSR_CX) == 0)
			panic("fpu ieee trap, but no exception");
		trapsignal(p, SIGFPE, fpu_codes[i - 1], fpu_types[i - 1], sv);
		return;
	case FSR_TT_UNFIN:
		if (instr.i_int == 0) {
#ifdef DEBUG
			printf("fpu_cleanup: unfinished fpop\n");
#endif
			return;
		}
		break;
	case FSR_TT_UNIMP:
		if (instr.i_int == 0)
			panic("fpu_cleanup: unimplemented fpop without insn");
		break;
	case FSR_TT_SEQ:
		panic("fpu sequence error");
		/* NOTREACHED */
	case FSR_TT_HWERR:
		log(LOG_ERR, "fpu hardware error (%s[%d])\n",
		    p->p_p->ps_comm, p->p_p->ps_pid);
		uprintf("%s[%d]: fpu hardware error\n",
		    p->p_p->ps_comm, p->p_p->ps_pid);
		trapsignal(p, SIGFPE, -1, FPE_FLTINV, sv);	/* ??? */
		return;
	default:
		printf("fsr=0x%x\n", fsr);
		panic("fpu error");
	}

	/* emulate the instructions left in the queue */
	fe.fe_fpstate = fs;
	if (instr.i_any.i_op != IOP_reg ||
	    (instr.i_op3.i_op3 != IOP3_FPop1 &&
	     instr.i_op3.i_op3 != IOP3_FPop2))
		panic("bogus fpu instruction to emulate");
	error = fpu_execute(p, &fe, instr);
	switch (error) {
	case 0:
		break;
	case FPE:
		trapsignal(p, SIGFPE,
		    fpu_codes[(fs->fs_fsr & FSR_CX) - 1],
		    fpu_types[(fs->fs_fsr & FSR_CX) - 1], sv);
		break;
	case NOTFPU:
	default:
		trapsignal(p, SIGILL, 0, ILL_COPROC, sv);
		break;
	}
}

/*
 * Compute offset given a register and type.  For 32 bit sparc, bits 1 and 0
 * must be zero for ext types, and bit 0 must be 0 for double and long types.
 * For 64bit sparc, bit 1 must be zero for quad types, and bit 0 becomes bit
 * 5 in the register offset for long, double, and quad types.
 */
int
fpu_regoffset(int rx, int type)
{
	if (type == FTYPE_LNG || type == FTYPE_DBL || type == FTYPE_EXT) {
		rx |= (rx & 1) << 5;
		rx &= 0x3e;
		if ((type == FTYPE_EXT) && (rx & 2))
			return (-1);
	}
	return (rx);
}

/*
 * Execute an FPU instruction (one that runs entirely in the FPU; not
 * FBfcc or STF, for instance).  On return, fe->fe_fs->fs_fsr will be
 * modified to reflect the setting the hardware would have left.
 */
int
fpu_execute(struct proc *p, struct fpemu *fe, union instr instr)
{
	struct fpstate *fs;
	int opf, rdtype, rd, err, mask, cx, fsr;
	u_int space[4];

	DPRINTF(FPE_INSN, ("op3: %x, opf %x\n", instr.i_opf.i_op3,
	    instr.i_opf.i_opf));
	DPRINTF(FPE_STATE, ("BEFORE:\n"));
	DUMPSTATE(FPE_STATE, fe->fe_fpstate);
	opf = instr.i_opf.i_opf;
	fs = fe->fe_fpstate;
	fe->fe_fsr = fs->fs_fsr & ~FSR_CX;
	fe->fe_cx = 0;

	if ((instr.i_int & 0xc0000000) != 0x80000000)
		return (NOTFPU);

	if (instr.i_opf.i_op3 == IOP3_FPop2) {
		switch (opf) {
		case FCMPS: case FCMPD: case FCMPQ:
			return (fpu_insn_fcmp(fs, fe, instr, 0));

		case FCMPES: case FCMPED: case FCMPEQ:
			return (fpu_insn_fcmp(fs, fe, instr, 1));

		case FMVFC0S: case FMVFC0D: case FMVFC0Q:
		case FMVFC1S: case FMVFC1D: case FMVFC1Q:
		case FMVFC2S: case FMVFC2D: case FMVFC2Q:
		case FMVFC3S: case FMVFC3D: case FMVFC3Q:
		case FMVICS: case FMVICD: case FMVICQ:
		case FMVXCS: case FMVXCD: case FMVXCQ:
			return (fpu_insn_fmovcc(p, fs, instr));

		case FMOVZS: case FMOVZD: case FMOVZQ:
		case FMOVLEZS: case FMOVLEZD: case FMOVLEZQ:
		case FMOVLZS: case FMOVLZD: case FMOVLZQ:
		case FMOVNZS: case FMOVNZD: case FMOVNZQ:
		case FMOVGZS: case FMOVGZD: case FMOVGZQ:
		case FMOVGEZS: case FMOVGEZD: case FMOVGEZQ:
			return (fpu_insn_fmovr(p, fs, instr));
		}
		return (NOTFPU);
	}

	if (instr.i_opf.i_op3 != IOP3_FPop1)
		return (NOTFPU);

	switch (instr.i_opf.i_opf) {
	case FSTOX: case FDTOX: case FQTOX:
		rdtype = FTYPE_LNG;
		if ((err = fpu_insn_ftoi(fe, instr, &rd, rdtype, space)) != 0)
			return (err);
		break;

	case FSTOI: case FDTOI: case FQTOI:
		rdtype = FTYPE_INT;
		if ((err = fpu_insn_ftoi(fe, instr, &rd, rdtype, space)) != 0)
			return (err);
		break;

	case FITOS: case FITOD: case FITOQ:
		if ((err = fpu_insn_itof(fe, instr, FTYPE_INT, &rd,
		    &rdtype, space)) != 0)
			return (err);
		break;

	case FXTOS: case FXTOD: case FXTOQ:
		if ((err = fpu_insn_itof(fe, instr, FTYPE_LNG, &rd,
		    &rdtype, space)) != 0)
			return (err);
		break;

	case FSTOD: case FSTOQ:
	case FDTOS: case FDTOQ:
	case FQTOS: case FQTOD:
		if ((err = fpu_insn_ftof(fe, instr, &rd, &rdtype, space)) != 0)
			return (err);
		break;

	case FMOVS: case FMOVD: case FMOVQ:
		return (fpu_insn_fmov(fs, fe, instr));

	case FNEGS: case FNEGD: case FNEGQ:
		return (fpu_insn_fneg(fs, fe, instr));

	case FABSS: case FABSD: case FABSQ:
		return (fpu_insn_fabs(fs, fe, instr));

	case FSQRTS: case FSQRTD: case FSQRTQ:
		if ((err = fpu_insn_fsqrt(fe, instr, &rd, &rdtype, space)) != 0)
			return (err);
		break;

	case FMULS: case FMULD: case FMULQ:
		if ((err = fpu_insn_fmul(fe, instr, &rd, &rdtype, space)) != 0)
			return (err);
		break;

	case FDIVS: case FDIVD: case FDIVQ:
		if ((err = fpu_insn_fdiv(fe, instr, &rd, &rdtype, space)) != 0)
			return (err);
		break;

	case FSMULD: case FDMULQ:
		if ((err = fpu_insn_fmulx(fe, instr, &rd, &rdtype, space)) != 0)
			return (err);
		break;

	case FADDS: case FADDD: case FADDQ:
		if ((err = fpu_insn_fadd(fe, instr, &rd, &rdtype, space)) != 0)
			return (err);
		break;

	case FSUBS: case FSUBD: case FSUBQ:
		if ((err = fpu_insn_fsub(fe, instr, &rd, &rdtype, space)) != 0)
			return (err);
		break;
	default:
		return (NOTFPU);
	}

	cx = fe->fe_cx;
	fsr = fe->fe_fsr;
	if (cx != 0) {
		mask = (fsr >> FSR_TEM_SHIFT) & FSR_TEM_MASK;
		if (cx & mask) {
			/* not accrued??? */
			fs->fs_fsr = (fsr & ~FSR_FTT) |
			    (FSR_TT_IEEE << FSR_FTT_SHIFT) |
			    (cx_to_trapx[(cx & mask) - 1] << FSR_CX_SHIFT);
			return (FPE);
		}
		fsr |= (cx << FSR_CX_SHIFT) | (cx << FSR_AX_SHIFT);
	}
	fs->fs_fsr = fsr;
	fpu_fcopy(space, fs->fs_regs + rd, rdtype);
	DPRINTF(FPE_STATE, ("AFTER:\n"));
	DUMPSTATE(FPE_STATE, fs);
	return (0);
}

/*
 * Handler for FMOV[SDQ] emulation.
 */
int
fpu_insn_fmov(struct fpstate *fs, struct fpemu *fe, union instr instr)
{
	int opf = instr.i_opf.i_opf, rs, rd, rtype;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rtype)) < 0)
		return (NOTFPU);
	fpu_fcopy(fs->fs_regs + rs, fs->fs_regs + rd, rtype);
	fs->fs_fsr = fe->fe_fsr;
	return (0);
}

/*
 * Handler for FABS[SDQ] emulation.
 */
int
fpu_insn_fabs(struct fpstate *fs, struct fpemu *fe, union instr instr)
{
	int opf = instr.i_opf.i_opf, rs, rd, rtype;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rtype)) < 0)
		return (NOTFPU);
	fpu_fcopy(fs->fs_regs + rs, fs->fs_regs + rd, rtype);
	fs->fs_regs[rd] = fs->fs_regs[rd] & ~(1U << 31);
	fs->fs_fsr = fe->fe_fsr;
	return (0);
}

/*
 * Handler for FNEG[SDQ] emulation.
 */
int
fpu_insn_fneg(struct fpstate *fs, struct fpemu *fe, union instr instr)
{
	int opf = instr.i_opf.i_opf, rs, rd, rtype;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rtype)) < 0)
		return (NOTFPU);
	fpu_fcopy(fs->fs_regs + rs, fs->fs_regs + rd, rtype);
	fs->fs_regs[rd] = fs->fs_regs[rd] ^ (1U << 31);
	fs->fs_fsr = fe->fe_fsr;
	return (0);
}

/*
 * Handler for F[XI]TO[SDQ] emulation.
 */
int
fpu_insn_itof(struct fpemu *fe, union instr instr, int rstype, int *rdp,
    int *rdtypep, u_int *space)
{
	int opf = instr.i_opf.i_opf, rs, rd, rdtype;

	if ((rs = fpu_regoffset(instr.i_opf.i_rs2, rstype)) < 0)
		return (NOTFPU);

	rdtype = (opf >> 2) & 3;
	if (rdtype == 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rdtype)) < 0)
		return (NOTFPU);

	DPRINTF(FPE_INSN, ("itof %%f%d(%d, %d) -> %%f%d(%d, %d)\n",
	    rs, rstype, instr.i_opf.i_rs2, rd, rdtype, instr.i_opf.i_rd));
	fpu_explode(fe, &fe->fe_f1, rstype, rs);
	fpu_implode(fe, &fe->fe_f1, rdtype, space);
	*rdp = rd;
	*rdtypep = rdtype;
	return (0);
}

/*
 * Handler for F[SDQ]TO[XI] emulation.
 */
int
fpu_insn_ftoi(struct fpemu *fe, union instr instr, int *rdp, int rdtype,
    u_int *space)
{
	int opf = instr.i_opf.i_opf, rd, rstype, rs;

	rstype = opf & 3;
	if (rstype == 0)
		return (NOTFPU);
	if ((rs = fpu_regoffset(instr.i_opf.i_rs2, rstype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rdtype)) < 0)
		return (NOTFPU);

	fpu_explode(fe, &fe->fe_f1, rstype, rs);
	fpu_implode(fe, &fe->fe_f1, rdtype, space);
	*rdp = rd;
	return (0);
}

/*
 * Handler for F[SDQ]TO[SDQ] emulation.
 */
int
fpu_insn_ftof(struct fpemu *fe, union instr instr, int *rdp, int *rdtypep,
    u_int *space)
{
	int opf = instr.i_opf.i_opf, rd, rs, rdtype, rstype;

	rstype = opf & 3;
	rdtype = (opf >> 2) & 3;

	if ((rstype == rdtype) || (rstype == 0) || (rdtype == 0))
		return (NOTFPU);

	if ((rs = fpu_regoffset(instr.i_opf.i_rs2, rstype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rdtype)) < 0)
		return (NOTFPU);

	DPRINTF(FPE_INSN, ("ftof %%f%d(%d, %d) -> %%f%d(%d, %d)\n",
	    rs, rstype, instr.i_opf.i_rs2, rd, rdtype, instr.i_opf.i_rd));

	fpu_explode(fe, &fe->fe_f1, rstype, rs);
	fpu_implode(fe, &fe->fe_f1, rdtype, space);
	*rdp = rd;
	*rdtypep = rdtype;
	return (0);
}

/*
 * Handler for FQSRT[SDQ] emulation.
 */
int
fpu_insn_fsqrt(struct fpemu *fe, union instr instr, int *rdp, int *rdtypep,
    u_int *space)
{
	int opf = instr.i_opf.i_opf, rd, rs, rtype;
	struct fpn *fp;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rtype)) < 0)
		return (NOTFPU);

	fpu_explode(fe, &fe->fe_f1, rtype, rs);
	fp = fpu_sqrt(fe);
	fpu_implode(fe, fp, rtype, space);
	*rdp = rd;
	*rdtypep = rtype;
	return (0);
}

/*
 * Handler for FCMP{E}[SDQ] emulation.
 */
int
fpu_insn_fcmp(struct fpstate *fs, struct fpemu *fe, union instr instr,
    int cmpe)
{
	int opf = instr.i_opf.i_opf, rs1, rs2, rtype, cx, fsr;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs1 = fpu_regoffset(instr.i_opf.i_rs1, rtype)) < 0)
		return (NOTFPU);
	if ((rs2 = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);

	fpu_explode(fe, &fe->fe_f1, rtype, rs1);
	fpu_explode(fe, &fe->fe_f2, rtype, rs2);
	fpu_compare(fe, cmpe);

	/*
	 * The only possible exception here is NV; catch it early
	 * and get out, as there is no result register.
	 */
	cx = fe->fe_cx;
	fsr = fe->fe_fsr | (cx << FSR_CX_SHIFT);
	if (cx != 0) {
		if (fsr & (FSR_NV << FSR_TEM_SHIFT)) {
			fs->fs_fsr = (fsr & ~FSR_FTT) |
			    (FSR_TT_IEEE << FSR_FTT_SHIFT);
			return (FPE);
		}
		fsr |= FSR_NV << FSR_AX_SHIFT;
	}
	fs->fs_fsr = fsr;
	return (0);
}

/*
 * Handler for FMUL[SDQ] emulation.
 */
int
fpu_insn_fmul(struct fpemu *fe, union instr instr, int *rdp, int *rdtypep,
    u_int *space)
{
	struct fpn *fp;
	int opf = instr.i_opf.i_opf, rd, rtype, rs1, rs2;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs1 = fpu_regoffset(instr.i_opf.i_rs1, rtype)) < 0)
		return (NOTFPU);
	if ((rs2 = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rtype)) < 0)
		return (NOTFPU);

	fpu_explode(fe, &fe->fe_f1, rtype, rs1);
	fpu_explode(fe, &fe->fe_f2, rtype, rs2);
	fp = fpu_mul(fe);
	fpu_implode(fe, fp, rtype, space);
	*rdp = rd;
	*rdtypep = rtype;
	return (0);
}

/*
 * Handler for FSMULD, FDMULQ emulation.
 */
int
fpu_insn_fmulx(struct fpemu *fe, union instr instr, int *rdp, int *rdtypep,
    u_int *space)
{
	struct fpn *fp;
	int opf = instr.i_opf.i_opf, rd, rdtype, rstype, rs1, rs2;

	rstype = opf & 3;
	rdtype = (opf >> 2) & 3;
	if ((rstype != rdtype + 1) || (rstype == 0) || (rdtype == 0))
		return (NOTFPU);
	if ((rs1 = fpu_regoffset(instr.i_opf.i_rs1, rstype)) < 0)
		return (NOTFPU);
	if ((rs2 = fpu_regoffset(instr.i_opf.i_rs2, rstype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rdtype)) < 0)
		return (NOTFPU);

	fpu_explode(fe, &fe->fe_f1, rstype, rs1);
	fpu_explode(fe, &fe->fe_f2, rstype, rs2);
	fp = fpu_mul(fe);
	fpu_implode(fe, fp, rdtype, space);
	*rdp = rd;
	*rdtypep = rdtype;
	return (0);
}

/*
 * Handler for FDIV[SDQ] emulation.
 */
int
fpu_insn_fdiv(struct fpemu *fe, union instr instr, int *rdp, int *rdtypep,
    u_int *space)
{
	struct fpn *fp;
	int opf = instr.i_opf.i_opf, rd, rtype, rs1, rs2;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs1 = fpu_regoffset(instr.i_opf.i_rs1, rtype)) < 0)
		return (NOTFPU);
	if ((rs2 = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rtype)) < 0)
		return (NOTFPU);

	fpu_explode(fe, &fe->fe_f1, rtype, rs1);
	fpu_explode(fe, &fe->fe_f2, rtype, rs2);
	fp = fpu_div(fe);
	fpu_implode(fe, fp, rtype, space);
	*rdp = rd;
	*rdtypep = rtype;
	return (0);
}

/*
 * Handler for FADD[SDQ] emulation.
 */
int
fpu_insn_fadd(struct fpemu *fe, union instr instr, int *rdp, int *rdtypep,
    u_int *space)
{
	struct fpn *fp;
	int opf = instr.i_opf.i_opf, rd, rtype, rs1, rs2;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs1 = fpu_regoffset(instr.i_opf.i_rs1, rtype)) < 0)
		return (NOTFPU);
	if ((rs2 = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rtype)) < 0)
		return (NOTFPU);

	fpu_explode(fe, &fe->fe_f1, rtype, rs1);
	fpu_explode(fe, &fe->fe_f2, rtype, rs2);
	fp = fpu_add(fe);
	fpu_implode(fe, fp, rtype, space);
	*rdp = rd;
	*rdtypep = rtype;
	return (0);
}

/*
 * Handler for FSUB[SDQ] emulation.
 */
int
fpu_insn_fsub(struct fpemu *fe, union instr instr, int *rdp, int *rdtypep,
    u_int *space)
{
	struct fpn *fp;
	int opf = instr.i_opf.i_opf, rd, rtype, rs1, rs2;

	rtype = opf & 3;
	if (rtype == 0)
		return (NOTFPU);
	if ((rs1 = fpu_regoffset(instr.i_opf.i_rs1, rtype)) < 0)
		return (NOTFPU);
	if ((rs2 = fpu_regoffset(instr.i_opf.i_rs2, rtype)) < 0)
		return (NOTFPU);
	if ((rd = fpu_regoffset(instr.i_opf.i_rd, rtype)) < 0)
		return (NOTFPU);

	fpu_explode(fe, &fe->fe_f1, rtype, rs1);
	fpu_explode(fe, &fe->fe_f2, rtype, rs2);
	fp = fpu_sub(fe);
	fpu_implode(fe, fp, rtype, space);
	*rdp = rd;
	*rdtypep = rtype;
	return (0);
}

/*
 * Handler for FMOV[SDQ][cond] emulation.
 */
int
fpu_insn_fmovcc(struct proc *p, struct fpstate *fs, union instr instr)
{
	int rtype, rd, rs, cond;

	rtype = instr.i_fmovcc.i_opf_low & 3;
	if ((rtype == 0) || (instr.i_int & 0x00040000))
		return (NOTFPU);

	if ((rd = fpu_regoffset(instr.i_fmovcc.i_rd, rtype)) < 0)
		return (NOTFPU);
	if ((rs = fpu_regoffset(instr.i_fmovcc.i_rs2, rtype)) < 0)
		return (NOTFPU);

	switch (instr.i_fmovcc.i_opf_cc) {
	case 0:
		cond = (fs->fs_fsr >> FSR_FCC_SHIFT) & FSR_FCC_MASK;
		break;
	case 1:
		cond = (fs->fs_fsr >> FSR_FCC1_SHIFT) & FSR_FCC_MASK;
		break;
	case 2:
		cond = (fs->fs_fsr >> FSR_FCC2_SHIFT) & FSR_FCC_MASK;
		break;
	case 3:
		cond = (fs->fs_fsr >> FSR_FCC3_SHIFT) & FSR_FCC_MASK;
		break;
	case 4:
		cond = (p->p_md.md_tf->tf_tstate >> TSTATE_CCR_SHIFT) &
		    PSR_ICC;
		break;
	case 6:
		cond = (p->p_md.md_tf->tf_tstate >>
		    (TSTATE_CCR_SHIFT + XCC_SHIFT)) & PSR_ICC;
		break;
	default:
		return (NOTFPU);
	}

	if (instr.i_fmovcc.i_cond != cond)
		return (0);

	fpu_fcopy(fs->fs_regs + rs, fs->fs_regs + rd, rtype);
	return (0);
}

/*
 * Handler for FMOVR[icond][SDQ] emulation.
 */
int
fpu_insn_fmovr(struct proc *p, struct fpstate *fs, union instr instr)
{
	int rtype, rd, rs2, rs1;

	rtype = instr.i_fmovcc.i_opf_low & 3;
	if ((rtype == 0) || (instr.i_int & 0x00002000))
		return (NOTFPU);

	if ((rd = fpu_regoffset(instr.i_fmovr.i_rd, rtype)) < 0)
		return (NOTFPU);
	if ((rs2 = fpu_regoffset(instr.i_fmovr.i_rs2, rtype)) < 0)
		return (NOTFPU);
	rs1 = instr.i_fmovr.i_rs1;

	switch (instr.i_fmovr.i_rcond) {
	case 1:	/* Z */
		if (rs1 != 0 &&
		    (int64_t)p->p_md.md_tf->tf_global[rs1] != 0)
			return (0);
		break;
	case 2: /* LEZ */
		if (rs1 != 0 &&
		    (int64_t)p->p_md.md_tf->tf_global[rs1] > 0)
			return (0);
		break;
	case 3: /* LZ */
		if (rs1 == 0 ||
		    (int64_t)p->p_md.md_tf->tf_global[rs1] >= 0)
			return (0);
		break;
	case 5:	/* NZ */
		if (rs1 == 0 ||
		    (int64_t)p->p_md.md_tf->tf_global[rs1] == 0)
			return (0);
		break;
	case 6: /* NGZ */
		if (rs1 == 0 ||
		    (int64_t)p->p_md.md_tf->tf_global[rs1] <= 0)
			return (0);
		break;
	case 7: /* NGEZ */
		if (rs1 != 0 &&
		    (int64_t)p->p_md.md_tf->tf_global[rs1] < 0)
			return (0);
		break;
	default:
		return (NOTFPU);
	}

	fpu_fcopy(fs->fs_regs + rs2, fs->fs_regs + rd, rtype);
	return (0);
}
