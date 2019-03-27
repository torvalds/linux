/*-
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	$NetBSD: fpu.c,v 1.5 2001/07/22 11:29:46 wiz Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/limits.h>

#include <machine/altivec.h>
#include <machine/fpu.h>
#include <machine/ieeefp.h>
#include <machine/pcb.h>
#include <machine/psl.h>

#include <powerpc/fpu/fpu_arith.h>
#include <powerpc/fpu/fpu_emu.h>
#include <powerpc/fpu/fpu_extern.h>

void spe_handle_fpdata(struct trapframe *);
void spe_handle_fpround(struct trapframe *);
static int spe_emu_instr(uint32_t, struct fpemu *, struct fpn **, uint32_t *);

static void
save_vec_int(struct thread *td)
{
	int	msr;
	struct	pcb *pcb;

	pcb = td->td_pcb;

	/*
	 * Temporarily re-enable the vector unit during the save
	 */
	msr = mfmsr();
	mtmsr(msr | PSL_VEC);

	/*
	 * Save the vector registers and SPEFSCR to the PCB
	 */
#define EVSTDW(n)   __asm ("evstdw %1,0(%0)" \
		:: "b"(pcb->pcb_vec.vr[n]), "n"(n));
	EVSTDW(0);	EVSTDW(1);	EVSTDW(2);	EVSTDW(3);
	EVSTDW(4);	EVSTDW(5);	EVSTDW(6);	EVSTDW(7);
	EVSTDW(8);	EVSTDW(9);	EVSTDW(10);	EVSTDW(11);
	EVSTDW(12);	EVSTDW(13);	EVSTDW(14);	EVSTDW(15);
	EVSTDW(16);	EVSTDW(17);	EVSTDW(18);	EVSTDW(19);
	EVSTDW(20);	EVSTDW(21);	EVSTDW(22);	EVSTDW(23);
	EVSTDW(24);	EVSTDW(25);	EVSTDW(26);	EVSTDW(27);
	EVSTDW(28);	EVSTDW(29);	EVSTDW(30);	EVSTDW(31);
#undef EVSTDW

	__asm ( "evxor 0,0,0\n"
		"evaddumiaaw 0,0\n"
		"evstdd 0,0(%0)" :: "b"(&pcb->pcb_vec.spare[0]));
	pcb->pcb_vec.vscr = mfspr(SPR_SPEFSCR);

	/*
	 * Disable vector unit again
	 */
	isync();
	mtmsr(msr);

}

void
enable_vec(struct thread *td)
{
	int	msr;
	struct	pcb *pcb;
	struct	trapframe *tf;

	pcb = td->td_pcb;
	tf = trapframe(td);

	/*
	 * Save the thread's SPE CPU number, and set the CPU's current
	 * vector thread
	 */
	td->td_pcb->pcb_veccpu = PCPU_GET(cpuid);
	PCPU_SET(vecthread, td);

	/*
	 * Enable the vector unit for when the thread returns from the
	 * exception. If this is the first time the unit has been used by
	 * the thread, initialise the vector registers and VSCR to 0, and
	 * set the flag to indicate that the vector unit is in use.
	 */
	tf->srr1 |= PSL_VEC;
	if (!(pcb->pcb_flags & PCB_VEC)) {
		memset(&pcb->pcb_vec, 0, sizeof pcb->pcb_vec);
		pcb->pcb_flags |= PCB_VEC;
		pcb->pcb_vec.vscr = mfspr(SPR_SPEFSCR);
	}

	/*
	 * Temporarily enable the vector unit so the registers
	 * can be restored.
	 */
	msr = mfmsr();
	mtmsr(msr | PSL_VEC);

	/* Restore SPEFSCR and ACC.  Use %r0 as the scratch for ACC. */
	mtspr(SPR_SPEFSCR, pcb->pcb_vec.vscr);
	__asm __volatile("evldd 0, 0(%0); evmra 0,0\n"
	    :: "b"(&pcb->pcb_vec.spare[0]));

	/* 
	 * The lower half of each register will be restored on trap return.  Use
	 * %r0 as a scratch register, and restore it last.
	 */
#define	EVLDW(n)   __asm __volatile("evldw 0, 0(%0); evmergehilo "#n",0,"#n \
	    :: "b"(&pcb->pcb_vec.vr[n]));
	EVLDW(1);	EVLDW(2);	EVLDW(3);	EVLDW(4);
	EVLDW(5);	EVLDW(6);	EVLDW(7);	EVLDW(8);
	EVLDW(9);	EVLDW(10);	EVLDW(11);	EVLDW(12);
	EVLDW(13);	EVLDW(14);	EVLDW(15);	EVLDW(16);
	EVLDW(17);	EVLDW(18);	EVLDW(19);	EVLDW(20);
	EVLDW(21);	EVLDW(22);	EVLDW(23);	EVLDW(24);
	EVLDW(25);	EVLDW(26);	EVLDW(27);	EVLDW(28);
	EVLDW(29);	EVLDW(30);	EVLDW(31);	EVLDW(0);
#undef EVLDW

	isync();
	mtmsr(msr);
}

void
save_vec(struct thread *td)
{
	struct pcb *pcb;

	pcb = td->td_pcb;

	save_vec_int(td);

	/*
	 * Clear the current vec thread and pcb's CPU id
	 * XXX should this be left clear to allow lazy save/restore ?
	 */
	pcb->pcb_veccpu = INT_MAX;
	PCPU_SET(vecthread, NULL);
}

/*
 * Save SPE state without dropping ownership.  This will only save state if
 * the current vector-thread is `td'.
 */
void
save_vec_nodrop(struct thread *td)
{
	struct thread *vtd;

	vtd = PCPU_GET(vecthread);
	if (td != vtd) {
		return;
	}

	save_vec_int(td);
}


#define	SPE_INST_MASK	0x31f
#define	EADD	0x200
#define	ESUB	0x201
#define	EABS	0x204
#define	ENABS	0x205
#define	ENEG	0x206
#define	EMUL	0x208
#define	EDIV	0x209
#define	ECMPGT	0x20c
#define	ECMPLT	0x20d
#define	ECMPEQ	0x20e
#define	ECFUI	0x210
#define	ECFSI	0x211
#define	ECTUI	0x214
#define	ECTSI	0x215
#define	ECTUF	0x216
#define	ECTSF	0x217
#define	ECTUIZ	0x218
#define	ECTSIZ	0x21a

#define	SPE		0x4
#define	SPFP		0x6
#define	DPFP		0x7

#define	SPE_OPC		4
#define	OPC_SHIFT	26

#define	EVFSADD		0x280
#define	EVFSSUB		0x281
#define	EVFSABS		0x284
#define	EVFSNABS	0x285
#define	EVFSNEG		0x286
#define	EVFSMUL		0x288
#define	EVFSDIV		0x289
#define	EVFSCMPGT	0x28c
#define	EVFSCMPLT	0x28d
#define	EVFSCMPEQ	0x28e
#define	EVFSCFUI	0x290
#define	EVFSCFSI	0x291
#define	EVFSCTUI	0x294
#define	EVFSCTSI	0x295
#define	EVFSCTUF	0x296
#define	EVFSCTSF	0x297
#define	EVFSCTUIZ	0x298
#define	EVFSCTSIZ	0x29a

#define	EFSADD		0x2c0
#define	EFSSUB		0x2c1
#define	EFSABS		0x2c4
#define	EFSNABS		0x2c5
#define	EFSNEG		0x2c6
#define	EFSMUL		0x2c8
#define	EFSDIV		0x2c9
#define	EFSCMPGT	0x2cc
#define	EFSCMPLT	0x2cd
#define	EFSCMPEQ	0x2ce
#define	EFSCFD		0x2cf
#define	EFSCFUI		0x2d0
#define	EFSCFSI		0x2d1
#define	EFSCTUI		0x2d4
#define	EFSCTSI		0x2d5
#define	EFSCTUF		0x2d6
#define	EFSCTSF		0x2d7
#define	EFSCTUIZ	0x2d8
#define	EFSCTSIZ	0x2da

#define	EFDADD		0x2e0
#define	EFDSUB		0x2e1
#define	EFDABS		0x2e4
#define	EFDNABS		0x2e5
#define	EFDNEG		0x2e6
#define	EFDMUL		0x2e8
#define	EFDDIV		0x2e9
#define	EFDCMPGT	0x2ec
#define	EFDCMPLT	0x2ed
#define	EFDCMPEQ	0x2ee
#define	EFDCFS		0x2ef
#define	EFDCFUI		0x2f0
#define	EFDCFSI		0x2f1
#define	EFDCTUI		0x2f4
#define	EFDCTSI		0x2f5
#define	EFDCTUF		0x2f6
#define	EFDCTSF		0x2f7
#define	EFDCTUIZ	0x2f8
#define	EFDCTSIZ	0x2fa

enum {
	NONE,
	SINGLE,
	DOUBLE,
	VECTOR,
};

static uint32_t fpscr_to_spefscr(uint32_t fpscr)
{
	uint32_t spefscr;

	spefscr = 0;

	if (fpscr & FPSCR_VX)
		spefscr |= SPEFSCR_FINV;
	if (fpscr & FPSCR_OX)
		spefscr |= SPEFSCR_FOVF;
	if (fpscr & FPSCR_UX)
		spefscr |= SPEFSCR_FUNF;
	if (fpscr & FPSCR_ZX)
		spefscr |= SPEFSCR_FDBZ;
	if (fpscr & FPSCR_XX)
		spefscr |= SPEFSCR_FX;

	return (spefscr);
}

/* Sign is 0 for unsigned, 1 for signed. */
static int
spe_to_int(struct fpemu *fpemu, struct fpn *fpn, uint32_t *val, int sign)
{
	uint32_t res[2];

	res[0] = fpu_ftox(fpemu, fpn, res);
	if (res[0] != UINT_MAX && res[0] != 0)
		fpemu->fe_cx |= FPSCR_OX;
	else if (sign == 0 && res[0] != 0)
		fpemu->fe_cx |= FPSCR_UX;
	else
		*val = res[1];

	return (0);
}

/* Masked instruction */
/*
 * For compare instructions, returns 1 if success, 0 if not.  For all others,
 * returns -1, or -2 if no result needs recorded.
 */
static int
spe_emu_instr(uint32_t instr, struct fpemu *fpemu,
    struct fpn **result, uint32_t *iresult)
{
	switch (instr & SPE_INST_MASK) {
	case EABS:
	case ENABS:
	case ENEG:
		/* Taken care of elsewhere. */
		break;
	case ECTUIZ:
		fpemu->fe_cx &= ~FPSCR_RN;
		fpemu->fe_cx |= FP_RZ;
	case ECTUI:
		spe_to_int(fpemu, &fpemu->fe_f2, iresult, 0);
		return (-1);
	case ECTSIZ:
		fpemu->fe_cx &= ~FPSCR_RN;
		fpemu->fe_cx |= FP_RZ;
	case ECTSI:
		spe_to_int(fpemu, &fpemu->fe_f2, iresult, 1);
		return (-1);
	case EADD:
		*result = fpu_add(fpemu);
		break;
	case ESUB:
		*result = fpu_sub(fpemu);
		break;
	case EMUL:
		*result = fpu_mul(fpemu);
		break;
	case EDIV:
		*result = fpu_div(fpemu);
		break;
	case ECMPGT:
		fpu_compare(fpemu, 0);
		if (fpemu->fe_cx & FPSCR_FG)
			return (1);
		return (0);
	case ECMPLT:
		fpu_compare(fpemu, 0);
		if (fpemu->fe_cx & FPSCR_FL)
			return (1);
		return (0);
	case ECMPEQ:
		fpu_compare(fpemu, 0);
		if (fpemu->fe_cx & FPSCR_FE)
			return (1);
		return (0);
	default:
		printf("Unknown instruction %x\n", instr);
	}

	return (-1);
}

static int
spe_explode(struct fpemu *fe, struct fpn *fp, uint32_t type,
    uint32_t hi, uint32_t lo)
{
	uint32_t s;

	fp->fp_sign = hi >> 31;
	fp->fp_sticky = 0;
	switch (type) {
	case SINGLE:
		s = fpu_stof(fp, hi);
		break;

	case DOUBLE:
		s = fpu_dtof(fp, hi, lo);
		break;
	}

	if (s == FPC_QNAN && (fp->fp_mant[0] & FP_QUIETBIT) == 0) {
		/*
		 * Input is a signalling NaN.  All operations that return
		 * an input NaN operand put it through a ``NaN conversion'',
		 * which basically just means ``turn on the quiet bit''.
		 * We do this here so that all NaNs internally look quiet
		 * (we can tell signalling ones by their class).
		 */
		fp->fp_mant[0] |= FP_QUIETBIT;
		fe->fe_cx = FPSCR_VXSNAN;	/* assert invalid operand */
		s = FPC_SNAN;
	}
	fp->fp_class = s;

	return (0);
}

/*
 * Save the high word of a 64-bit GPR for manipulation in the exception handler.
 */
static uint32_t
spe_save_reg_high(int reg)
{
	uint32_t vec[2];
#define EVSTDW(n)   case n: __asm __volatile ("evstdw %1,0(%0)" \
		:: "b"(vec), "n"(n)); break;
	switch (reg) {
	EVSTDW(0);	EVSTDW(1);	EVSTDW(2);	EVSTDW(3);
	EVSTDW(4);	EVSTDW(5);	EVSTDW(6);	EVSTDW(7);
	EVSTDW(8);	EVSTDW(9);	EVSTDW(10);	EVSTDW(11);
	EVSTDW(12);	EVSTDW(13);	EVSTDW(14);	EVSTDW(15);
	EVSTDW(16);	EVSTDW(17);	EVSTDW(18);	EVSTDW(19);
	EVSTDW(20);	EVSTDW(21);	EVSTDW(22);	EVSTDW(23);
	EVSTDW(24);	EVSTDW(25);	EVSTDW(26);	EVSTDW(27);
	EVSTDW(28);	EVSTDW(29);	EVSTDW(30);	EVSTDW(31);
	}
#undef EVSTDW

	return (vec[0]);
}

/*
 * Load the given value into the high word of the requested register.
 */
static void
spe_load_reg_high(int reg, uint32_t val)
{
#define	EVLDW(n)   case n: __asm __volatile("evmergelo "#n",%0,"#n \
	    :: "r"(val)); break;
	switch (reg) {
	EVLDW(1);	EVLDW(2);	EVLDW(3);	EVLDW(4);
	EVLDW(5);	EVLDW(6);	EVLDW(7);	EVLDW(8);
	EVLDW(9);	EVLDW(10);	EVLDW(11);	EVLDW(12);
	EVLDW(13);	EVLDW(14);	EVLDW(15);	EVLDW(16);
	EVLDW(17);	EVLDW(18);	EVLDW(19);	EVLDW(20);
	EVLDW(21);	EVLDW(22);	EVLDW(23);	EVLDW(24);
	EVLDW(25);	EVLDW(26);	EVLDW(27);	EVLDW(28);
	EVLDW(29);	EVLDW(30);	EVLDW(31);	EVLDW(0);
	}
#undef EVLDW

}

void
spe_handle_fpdata(struct trapframe *frame)
{
	struct fpemu fpemu;
	struct fpn *result;
	uint32_t instr, instr_sec_op;
	uint32_t cr_shift, ra, rb, rd, src;
	uint32_t high, low, res, tmp; /* For vector operations. */
	uint32_t spefscr = 0;
	uint32_t ftod_res[2];
	int width; /* Single, Double, Vector, Integer */
	int err;
	uint32_t msr;

	err = fueword32((void *)frame->srr0, &instr);
	
	if (err != 0)
		return;
		/* Fault. */;

	if ((instr >> OPC_SHIFT) != SPE_OPC)
		return;

	msr = mfmsr();
	/*
	 * 'cr' field is the upper 3 bits of rd.  Magically, since a) rd is 5
	 * bits, b) each 'cr' field is 4 bits, and c) Only the 'GT' bit is
	 * modified for most compare operations, the full value of rd can be
	 * used as a shift value.
	 */
	rd = (instr >> 21) & 0x1f;
	ra = (instr >> 16) & 0x1f;
	rb = (instr >> 11) & 0x1f;
	src = (instr >> 5) & 0x7;
	cr_shift = 28 - (rd & 0x1f);

	instr_sec_op = (instr & 0x7ff);

	memset(&fpemu, 0, sizeof(fpemu));

	width = NONE;
	switch (src) {
	case SPE:
		mtmsr(msr | PSL_VEC);
		switch (instr_sec_op) {
		case EVFSABS:
			high = spe_save_reg_high(ra) & ~(1U << 31);
			frame->fixreg[rd] = frame->fixreg[ra] & ~(1U << 31);
			spe_load_reg_high(rd, high);
			break;
		case EVFSNABS:
			high = spe_save_reg_high(ra) | (1U << 31);
			frame->fixreg[rd] = frame->fixreg[ra] | (1U << 31);
			spe_load_reg_high(rd, high);
			break;
		case EVFSNEG:
			high = spe_save_reg_high(ra) ^ (1U << 31);
			frame->fixreg[rd] = frame->fixreg[ra] ^ (1U << 31);
			spe_load_reg_high(rd, high);
			break;
		default:
			/* High word */
			spe_explode(&fpemu, &fpemu.fe_f1, SINGLE,
			    spe_save_reg_high(ra), 0);
			spe_explode(&fpemu, &fpemu.fe_f2, SINGLE,
			    spe_save_reg_high(rb), 0);
			high = spe_emu_instr(instr_sec_op, &fpemu, &result,
			    &tmp);

			if (high < 0)
				spe_load_reg_high(rd, tmp);

			spefscr = fpscr_to_spefscr(fpemu.fe_cx) << 16;
			/* Clear the fpemu to start over on the lower bits. */
			memset(&fpemu, 0, sizeof(fpemu));

			/* Now low word */
			spe_explode(&fpemu, &fpemu.fe_f1, SINGLE,
			    frame->fixreg[ra], 0);
			spe_explode(&fpemu, &fpemu.fe_f2, SINGLE,
			    frame->fixreg[rb], 0);
			spefscr |= fpscr_to_spefscr(fpemu.fe_cx);
			low = spe_emu_instr(instr_sec_op, &fpemu, &result,
			    &frame->fixreg[rd]);
			if (instr_sec_op == EVFSCMPEQ ||
			    instr_sec_op == EVFSCMPGT ||
			    instr_sec_op == EVFSCMPLT) {
				res = (high << 3) | (low << 2) |
				    ((high | low) << 1) | (high & low);
				width = NONE;
			} else
				width = VECTOR;
			break;
		}
		goto end;

	case SPFP:
		switch (instr_sec_op) {
		case EFSABS:
			frame->fixreg[rd] = frame->fixreg[ra] & ~(1U << 31);
			break;
		case EFSNABS:
			frame->fixreg[rd] = frame->fixreg[ra] | (1U << 31);
			break;
		case EFSNEG:
			frame->fixreg[rd] = frame->fixreg[ra] ^ (1U << 31);
			break;
		case EFSCFD:
			spe_explode(&fpemu, &fpemu.fe_f3, DOUBLE,
			    spe_save_reg_high(rb), frame->fixreg[rb]);
			result = &fpemu.fe_f3;
			width = SINGLE;
			break;
		default:
			spe_explode(&fpemu, &fpemu.fe_f1, SINGLE,
			    frame->fixreg[ra], 0);
			spe_explode(&fpemu, &fpemu.fe_f2, SINGLE,
			    frame->fixreg[rb], 0);
			width = SINGLE;
		}
		break;
	case DPFP:
		mtmsr(msr | PSL_VEC);
		switch (instr_sec_op) {
		case EFDABS:
			high = spe_save_reg_high(ra) & ~(1U << 31);
			frame->fixreg[rd] = frame->fixreg[ra];
			spe_load_reg_high(rd, high);
			break;
		case EFDNABS:
			high = spe_save_reg_high(ra) | (1U << 31);
			frame->fixreg[rd] = frame->fixreg[ra];
			spe_load_reg_high(rd, high);
			break;
		case EFDNEG:
			high = spe_save_reg_high(ra) ^ (1U << 31);
			frame->fixreg[rd] = frame->fixreg[ra];
			spe_load_reg_high(rd, high);
			break;
		case EFDCFS:
			spe_explode(&fpemu, &fpemu.fe_f3, SINGLE,
			    frame->fixreg[rb], 0);
			result = &fpemu.fe_f3;
			width = DOUBLE;
			break;
		default:
			spe_explode(&fpemu, &fpemu.fe_f1, DOUBLE,
			    spe_save_reg_high(ra), frame->fixreg[ra]);
			spe_explode(&fpemu, &fpemu.fe_f2, DOUBLE,
			    spe_save_reg_high(rb), frame->fixreg[rb]);
			width = DOUBLE;
		}
		break;
	}
	switch (instr_sec_op) {
	case EFDCFS:
	case EFSCFD:
		/* Already handled. */
		break;
	default:
		res = spe_emu_instr(instr_sec_op, &fpemu, &result,
		    &frame->fixreg[rd]);
		if (res != -1)
			res <<= 2;
		break;
	}

	switch (instr_sec_op & SPE_INST_MASK) {
	case ECMPEQ:
	case ECMPGT:
	case ECMPLT:
		frame->cr &= ~(0xf << cr_shift);
		frame->cr |= (res << cr_shift);
		break;
	case ECTUI:
	case ECTUIZ:
	case ECTSI:
	case ECTSIZ:
		break;
	default:
		switch (width) {
		case NONE:
		case VECTOR:
			break;
		case SINGLE:
			frame->fixreg[rd] = fpu_ftos(&fpemu, result);
			break;
		case DOUBLE:
			spe_load_reg_high(rd, fpu_ftod(&fpemu, result, ftod_res));
			frame->fixreg[rd] = ftod_res[1];
			break;
		default:
			panic("Unknown storage width %d", width);
			break;
		}
	}

end:
	spefscr |= (mfspr(SPR_SPEFSCR) & ~SPEFSCR_FINVS);
	mtspr(SPR_SPEFSCR, spefscr);
	frame->srr0 += 4;
	mtmsr(msr);

	return;
}

void
spe_handle_fpround(struct trapframe *frame)
{

	/*
	 * Punt fpround exceptions for now.  This leaves the truncated result in
	 * the register.  We'll deal with overflow/underflow later.
	 */
	return;
}
