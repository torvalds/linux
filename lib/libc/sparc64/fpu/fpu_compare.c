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
 *
 *	@(#)fpu_compare.c	8.1 (Berkeley) 6/11/93
 *	$NetBSD: fpu_compare.c,v 1.3 2001/08/26 05:46:31 eeh Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * CMP and CMPE instructions.
 *
 * These rely on the fact that our internal wide format is achieved by
 * adding zero bits to the end of narrower mantissas.
 */

#include <sys/types.h>

#include <machine/frame.h>
#include <machine/fp.h>
#include <machine/fsr.h>

#include "fpu_arith.h"
#include "fpu_emu.h"
#include "fpu_extern.h"

static u_long fcc_nmask[] = {
	~FSR_FCC0_MASK,
	~FSR_FCC1_MASK,
	~FSR_FCC2_MASK,
	~FSR_FCC3_MASK
};

/* XXX: we don't use the FSR_FCCx macros here; it's much easier this way. */
static int fcc_shift[] = {
	FSR_FCC0_SHIFT,
	FSR_FCC1_SHIFT,
	FSR_FCC2_SHIFT,
	FSR_FCC3_SHIFT
};

/*
 * Perform a compare instruction (with or without unordered exception).
 * This updates the fcc field in the fsr.
 *
 * If either operand is NaN, the result is unordered.  For cmpe, this
 * causes an NV exception.  Everything else is ordered:
 *	|Inf| > |numbers| > |0|.
 * We already arranged for fp_class(Inf) > fp_class(numbers) > fp_class(0),
 * so we get this directly.  Note, however, that two zeros compare equal
 * regardless of sign, while everything else depends on sign.
 *
 * Incidentally, two Infs of the same sign compare equal (per the 80387
 * manual---it would be nice if the SPARC documentation were more
 * complete).
 */
void
__fpu_compare(struct fpemu *fe, int cmpe, int fcc)
{
	struct fpn *a, *b;
	int cc;
	FPU_DECL_CARRY

	a = &fe->fe_f1;
	b = &fe->fe_f2;

	if (ISNAN(a) || ISNAN(b)) {
		/*
		 * In any case, we already got an exception for signalling
		 * NaNs; here we may replace that one with an identical
		 * exception, but so what?.
		 */
		if (cmpe)
			fe->fe_cx = FSR_NV;
		cc = FSR_CC_UO;
		goto done;
	}

	/*
	 * Must handle both-zero early to avoid sign goofs.  Otherwise,
	 * at most one is 0, and if the signs differ we are done.
	 */
	if (ISZERO(a) && ISZERO(b)) {
		cc = FSR_CC_EQ;
		goto done;
	}
	if (a->fp_sign) {		/* a < 0 (or -0) */
		if (!b->fp_sign) {	/* b >= 0 (or if a = -0, b > 0) */
			cc = FSR_CC_LT;
			goto done;
		}
	} else {			/* a > 0 (or +0) */
		if (b->fp_sign) {	/* b <= -0 (or if a = +0, b < 0) */
			cc = FSR_CC_GT;
			goto done;
		}
	}

	/*
	 * Now the signs are the same (but may both be negative).  All
	 * we have left are these cases:
	 *
	 *	|a| < |b|		[classes or values differ]
	 *	|a| > |b|		[classes or values differ]
	 *	|a| == |b|		[classes and values identical]
	 *
	 * We define `diff' here to expand these as:
	 *
	 *	|a| < |b|, a,b >= 0: a < b => FSR_CC_LT
	 *	|a| < |b|, a,b < 0:  a > b => FSR_CC_GT
	 *	|a| > |b|, a,b >= 0: a > b => FSR_CC_GT
	 *	|a| > |b|, a,b < 0:  a < b => FSR_CC_LT
	 */
#define opposite_cc(cc) ((cc) == FSR_CC_LT ? FSR_CC_GT : FSR_CC_LT)
#define	diff(magnitude) (a->fp_sign ? opposite_cc(magnitude) :  (magnitude))
	if (a->fp_class < b->fp_class) {	/* |a| < |b| */
		cc = diff(FSR_CC_LT);
		goto done;
	}
	if (a->fp_class > b->fp_class) {	/* |a| > |b| */
		cc = diff(FSR_CC_GT);
		goto done;
	}
	/* now none can be 0: only Inf and numbers remain */
	if (ISINF(a)) {				/* |Inf| = |Inf| */
		cc = FSR_CC_EQ;
		goto done;
	}
	/*
	 * Only numbers remain.  To compare two numbers in magnitude, we
	 * simply subtract them.
	 */
	a = __fpu_sub(fe);
	if (a->fp_class == FPC_ZERO)
		cc = FSR_CC_EQ;
	else
		cc = diff(FSR_CC_GT);

done:
	fe->fe_fsr = (fe->fe_fsr & fcc_nmask[fcc]) |
	    ((u_long)cc << fcc_shift[fcc]);
}
