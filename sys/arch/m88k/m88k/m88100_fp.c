/*	$OpenBSD: m88100_fp.c,v 1.9 2025/07/16 07:15:42 jsg Exp $	*/

/*
 * Copyright (c) 2007, 2014, Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/systm.h>

#include <machine/fpu.h>
#include <machine/frame.h>
#include <machine/ieee.h>
#include <machine/ieeefp.h>
#include <machine/trap.h>
#include <machine/m88100.h>

#include <lib/libkern/softfloat.h>
#include <lib/libkern/milieu.h>
float32 normalizeRoundAndPackFloat32(int, int16, bits32);
float64 normalizeRoundAndPackFloat64(flag, int16, bits64);

#include <m88k/m88k/fpu.h>

int	m88100_fpu_emulate(struct trapframe *);
void	m88100_fpu_fetch(struct trapframe *, u_int, u_int, u_int, fparg *);
void	m88100_fpu_checksig(struct trapframe *, int, int);

/*
 * All 88100 precise floating-point exceptions are handled there.
 *
 * We ignore the exception cause register completely, except for the
 * `privilege violation' bit, and attempt to perform the computation in
 * software if needed.
 */

void
m88100_fpu_precise_exception(struct trapframe *frame)
{
	int fault_type;
	int sig;

	/* if FPECR_FUNIMP is set, all other bits are undefined, ignore them */
	if (ISSET(frame->tf_fpecr, FPECR_FUNIMP))
		frame->tf_fpecr = FPECR_FUNIMP;

	/* Reset the exception cause register */
	__asm__ volatile ("fstcr %r0, %fcr0");

	if (ISSET(frame->tf_fpecr, FPECR_FPRV)) {
		sig = SIGILL;
		fault_type = ILL_PRVREG;
	} else {
		sig = m88100_fpu_emulate(frame);
		fault_type = SI_NOINFO;
	}

	/*
	 * Update the floating point status register regardless of
	 * whether we'll deliver a signal or not.
	 */
	__asm__ volatile ("fstcr %0, %%fcr62" :: "r"(frame->tf_fpsr));

	m88100_fpu_checksig(frame, sig, fault_type);
}

/*
 * Convert a single floating-point argument with its exponent sign-extended
 * to 11 bits in an fphs/fpls pair to a correct 32-bit single precision
 * number.
 */
static inline uint32_t
m88100_fpu_parse_single(uint32_t hs, uint32_t ls)
{
	uint32_t result;

	result = hs << (DBL_EXPBITS - SNG_EXPBITS);
	result &= ~(1U << 31);		/* clear carry into sign bit */
	result |= ls >> (DBL_FRACBITS - SNG_FRACBITS);
	result |= hs & (1U << 31);	/* sign bit */

	return result;
}

/*
 * Load a floating-point argument into a fparg union, then convert it to
 * the required format if it is of larger precision.
 *
 * This assumes the final format (width) is not FTYPE_INT, and the original
 * format (orig_width) <= width.
 */
void
m88100_fpu_fetch(struct trapframe *frame, u_int operandno, u_int orig_width,
    u_int width, fparg *dest)
{
	u_int32_t tmp;

	switch (orig_width) {
	case FTYPE_INT:
		tmp = operandno == 1 ? frame->tf_fpls1 : frame->tf_fpls2;
		switch (width) {
		case FTYPE_SNG:
			dest->sng = int32_to_float32(tmp);
			break;
		case FTYPE_DBL:
			dest->dbl = int32_to_float64(tmp);
			break;
		}
		break;
	case FTYPE_SNG:
		tmp = operandno == 1 ?
		    m88100_fpu_parse_single(frame->tf_fphs1, frame->tf_fpls1) :
		    m88100_fpu_parse_single(frame->tf_fphs2, frame->tf_fpls2);
		switch (width) {
		case FTYPE_SNG:
			dest->sng = tmp;
			break;
		case FTYPE_DBL:
			dest->dbl = float32_to_float64(tmp);
			break;
		}
		break;
	case FTYPE_DBL:
		tmp = operandno == 1 ? frame->tf_fphs1 : frame->tf_fphs2;
		dest->dbl = ((float64)tmp) << 32;
		tmp = operandno == 1 ? frame->tf_fpls1 : frame->tf_fpls2;
		dest->dbl |= (float64)tmp;
		break;
	}
}

/*
 * Emulate an FPU instruction.  On return, the trapframe registers
 * will be modified to reflect the settings the hardware would have left.
 */
int
m88100_fpu_emulate(struct trapframe *frame)
{
	u_int rd, t1, t2, td, tmax, opcode;
	u_int32_t old_fpsr, old_fpcr;
	int rc;

	fparg arg1, arg2, dest;

	/*
	 * Crack the instruction.
	 */
	rd = frame->tf_fppt & 0x1f;
	opcode = (frame->tf_fppt >> 11) & 0x1f;
	t1 = (frame->tf_fppt >> 9) & 0x03;
	t2 = (frame->tf_fppt >> 7) & 0x03;
	td = (frame->tf_fppt >> 5) & 0x03;

	if (rd == 0)	/* r0 not allowed as destination */
		return (SIGILL);

	switch (opcode) {
	case 0x00:	/* fmul */
	case 0x05:	/* fadd */
	case 0x06:	/* fsub */
	case 0x0e:	/* fdiv */
		if ((t1 != FTYPE_SNG && t1 != FTYPE_DBL) ||
		    (t2 != FTYPE_SNG && t2 != FTYPE_DBL) ||
		    (td != FTYPE_SNG && td != FTYPE_DBL))
			return (SIGILL);
		break;
	case 0x04:	/* flt */
		if ((td != FTYPE_SNG && td != FTYPE_DBL) ||
		    t2 != 0x00 || t1 != 0x00)
			return (SIGILL);
		break;
	case 0x07:	/* fcmp */
		if ((t1 != FTYPE_SNG && t1 != FTYPE_DBL) ||
		    (t2 != FTYPE_SNG && t2 != FTYPE_DBL) ||
		    td != 0x00)
			return (SIGILL);
		break;
	case 0x09:	/* int */
	case 0x0a:	/* nint */
	case 0x0b:	/* trnc */
		if ((t2 != FTYPE_SNG && t2 != FTYPE_DBL) ||
		    t1 != 0x00 || td != 0x00)
			return (SIGILL);
		break;
	default:
		return (SIGILL);
	}

	/*
	 * Temporarily reset the status register, so that we can tell
	 * which exceptions are new after processing the opcode.
	 */
	old_fpsr = frame->tf_fpsr;
	frame->tf_fpsr = 0;

	/*
	 * Save fpcr as well, since we might need to change rounding mode
	 * temporarily.
	 */
	old_fpcr = frame->tf_fpcr;

	/*
	 * The logic for instruction emulation is:
	 *
	 * - the computation precision is the largest one of all the operands.
	 * - all source operands are converted to this precision if needed.
	 * - computation is performed.
	 * - the result is stored into the destination operand, converting it
	 *   to the destination precision if lower.
	 */

	switch (opcode) {
	case 0x00:	/* fmul */
		tmax = fpu_precision(t1, t2, td);
		m88100_fpu_fetch(frame, 1, t1, tmax, &arg1);
		m88100_fpu_fetch(frame, 2, t2, tmax, &arg2);
		switch (tmax) {
		case FTYPE_SNG:
			dest.sng = float32_mul(arg1.sng, arg2.sng);
			break;
		case FTYPE_DBL:
			dest.dbl = float64_mul(arg1.dbl, arg2.dbl);
			break;
		}
		fpu_store(frame, rd, tmax, td, &dest);
		break;

	case 0x04:	/* flt */
		m88100_fpu_fetch(frame, 2, FTYPE_INT, td, &dest);
		fpu_store(frame, rd, td, td, &dest);
		break;

	case 0x05:	/* fadd */
		tmax = fpu_precision(t1, t2, td);
		m88100_fpu_fetch(frame, 1, t1, tmax, &arg1);
		m88100_fpu_fetch(frame, 2, t2, tmax, &arg2);
		switch (tmax) {
		case FTYPE_SNG:
			dest.sng = float32_add(arg1.sng, arg2.sng);
			break;
		case FTYPE_DBL:
			dest.dbl = float64_add(arg1.dbl, arg2.dbl);
			break;
		}
		fpu_store(frame, rd, tmax, td, &dest);
		break;

	case 0x06:	/* fsub */
		tmax = fpu_precision(t1, t2, td);
		m88100_fpu_fetch(frame, 1, t1, tmax, &arg1);
		m88100_fpu_fetch(frame, 2, t2, tmax, &arg2);
		switch (tmax) {
		case FTYPE_SNG:
			dest.sng = float32_sub(arg1.sng, arg2.sng);
			break;
		case FTYPE_DBL:
			dest.dbl = float64_sub(arg1.dbl, arg2.dbl);
			break;
		}
		fpu_store(frame, rd, tmax, td, &dest);
		break;

	case 0x07:	/* fcmp */
		tmax = fpu_precision(t1, t2, IGNORE_PRECISION);
		m88100_fpu_fetch(frame, 1, t1, tmax, &arg1);
		m88100_fpu_fetch(frame, 2, t2, tmax, &arg2);
		fpu_compare(frame, &arg1, &arg2, tmax, rd, 0);
		break;

	case 0x09:	/* int */
do_int:
		m88100_fpu_fetch(frame, 2, t2, t2, &dest);
		fpu_store(frame, rd, t2, FTYPE_INT, &dest);
		break;

	case 0x0a:	/* nint */
		/* round to nearest */
		frame->tf_fpcr = (old_fpcr & ~(FPCR_RD_MASK << FPCR_RD_SHIFT)) |
		    (FP_RN << FPCR_RD_SHIFT);
		goto do_int;

	case 0x0b:	/* trnc */
		/* round towards zero */
		frame->tf_fpcr = (old_fpcr & ~(FPCR_RD_MASK << FPCR_RD_SHIFT)) |
		    (FP_RZ << FPCR_RD_SHIFT);
		goto do_int;

	case 0x0e:	/* fdiv */
		tmax = fpu_precision(t1, t2, td);
		m88100_fpu_fetch(frame, 1, t1, tmax, &arg1);
		m88100_fpu_fetch(frame, 2, t2, tmax, &arg2);
		switch (tmax) {
		case FTYPE_SNG:
			dest.sng = float32_div(arg1.sng, arg2.sng);
			break;
		case FTYPE_DBL:
			dest.dbl = float64_div(arg1.dbl, arg2.dbl);
			break;
		}
		fpu_store(frame, rd, tmax, td, &dest);
		break;
	}

	/*
	 * Mark new exceptions, if any, in the fpsr, and decide whether
	 * to send a signal or not.
	 */

	if (frame->tf_fpsr & old_fpcr)
		rc = SIGFPE;
	else
		rc = 0;
	frame->tf_fpsr |= old_fpsr;

	/*
	 * Restore fpcr as well.
	 */
	frame->tf_fpcr = old_fpcr;

	return (rc);
}

/*
 * All 88100 imprecise floating-point exceptions are handled there.
 *
 * We ignore the exception condition bits and simply round the intermediate
 * result according to the current rounding mode, raising whichever exception
 * conditions are necessary in the process.
 */

void
m88100_fpu_imprecise_exception(struct trapframe *frame)
{
	flag sign;
	int16 exp;
	bits32 mant32;
	bits64 mant64;
	fparg res;
	u_int fmt;

	/* Reset the exception cause register */
	__asm__ volatile ("fstcr %r0, %fcr0");

	/*
	 * The 88100 errata for mask C82N (rev 0x0a) documents that an
	 * imprecise exception may be raised for integer instructions
	 * returning an inexact result.
	 * However, there is nothing to do in this case, since the result
	 * is not a floating-point value, and has been correctly put in
	 * the destination register; we simply need to ignore that
	 * exception.
	 */
	switch ((frame->tf_fpit >> 11) & 0x1f) {
	case 0x09:	/* int */
	case 0x0a:	/* nint */
	case 0x0b:	/* trnc */
		return;
	default:
		break;
	}

	/*
	 * Pick the inexact result, build a float32 or a float64 out of it, and
	 * normalize it to the destination width.
	 */

	if (frame->tf_fpit & FPIT_DBL)
		fmt = FTYPE_DBL;
	else
		fmt = FTYPE_SNG;

	sign = (frame->tf_fprh & FPRH_SIGN) != 0;
	exp = ((int32_t)frame->tf_fpit) >> 20; /* signed, unbiased exponent */

	if (fmt == FTYPE_SNG) {
		exp += SNG_EXP_BIAS;
		mant32 = (frame->tf_fprh & FPRH_MANTH_MASK);
	       	mant32 <<= (SNG_FRACBITS - FPRH_MANTH_BITS + 1);
		mant32 |= frame->tf_fprl >> (DBL_FRACBITS - SNG_FRACBITS);

		/*
		 * If the mantissa has been incremented, revert this; but
		 * if doing so causes the mantissa hidden bit to clear,
		 * the exponent needs to be decremented and the hidden bit
		 * restored.
		 */
		if (frame->tf_fprh & FPRH_ADDONE) {
			mant32--;
			if ((mant32 & (1 << SNG_FRACBITS)) == 0) {
				exp--;
				mant32 |= 1 << SNG_FRACBITS;
			}
		}

		/* normalizeRoundAndPackFloat32() requirement */
		mant32 <<= (31 - SNG_FRACBITS - 1);
		res.sng = normalizeRoundAndPackFloat32(sign, exp, mant32);
	} else {
		exp += DBL_EXP_BIAS;
		mant64 = frame->tf_fprh & FPRH_MANTH_MASK;
		mant64 <<= (DBL_FRACBITS - FPRH_MANTH_BITS + 1);
		mant64 |= frame->tf_fprl;

		/*
		 * If the mantissa has been incremented, revert this; but
		 * if doing so causes the mantissa hidden bit to clear,
		 * the exponent needs to be decremented and the hidden bit
		 * restored.
		 */
		if (frame->tf_fprh & FPRH_ADDONE) {
			mant64--;
			if ((mant64 & (1LL << DBL_FRACBITS)) == 0) {
				exp--;
				mant64 |= 1LL << DBL_FRACBITS;
			}
		}

		/* normalizeRoundAndPackFloat64() requirement */
		mant64 <<= (63 - DBL_FRACBITS - 1);
		res.dbl = normalizeRoundAndPackFloat64(sign, exp, mant64);
	}

	fpu_store(frame, frame->tf_fpit & 0x1f, fmt, fmt, &res);

	/*
	 * Update the floating point status register regardless of
	 * whether we'll deliver a signal or not.
	 */
	__asm__ volatile ("fstcr %0, %%fcr62" :: "r"(frame->tf_fpsr));

	/*
	 * Check for a SIGFPE condition.
	 *
	 * XXX If the exception was caught while in kernel mode, we can't
	 * XXX send a signal at this point... what to do?
	 */
	if ((frame->tf_fpsr & PSR_MODE) == 0) {
		if (frame->tf_fpsr & frame->tf_fpcr)
			m88100_fpu_checksig(frame, SIGFPE, 0 /* SI_NOINFO */);
	}
}

/*
 * Check if a signal needs to be delivered, and send it.
 */
void
m88100_fpu_checksig(struct trapframe *frame, int sig, int fault_type)
{
	struct proc *p = curproc;
	union sigval sv;

	if (sig != 0) {
		if (sig == SIGILL) {
			if (fault_type == SI_NOINFO)
				fault_type = ILL_ILLOPC;
		} else {
			if (frame->tf_fpecr & FPECR_FIOV)
				fault_type = FPE_FLTSUB;
			else if (frame->tf_fpecr & FPECR_FROP)
				fault_type = FPE_FLTINV;
			else if (frame->tf_fpecr & FPECR_FDVZ)
				fault_type = FPE_INTDIV;
			else if (frame->tf_fpecr & FPECR_FUNF) {
				if (frame->tf_fpsr & FPSR_EFUNF)
					fault_type = FPE_FLTUND;
				else if (frame->tf_fpsr & FPSR_EFINX)
					fault_type = FPE_FLTRES;
			} else if (frame->tf_fpecr & FPECR_FOVF) {
				if (frame->tf_fpsr & FPSR_EFOVF)
					fault_type = FPE_FLTOVF;
				else if (frame->tf_fpsr & FPSR_EFINX)
					fault_type = FPE_FLTRES;
			} else if (frame->tf_fpecr & FPECR_FINX)
				fault_type = FPE_FLTRES;
		}

		sv.sival_ptr = (void *)(frame->tf_sxip & XIP_ADDR);
		trapsignal(p, sig, 0, fault_type, sv);
	}
}
