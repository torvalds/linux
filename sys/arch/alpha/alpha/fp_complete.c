/*	$OpenBSD: fp_complete.c,v 1.14 2025/06/29 15:55:21 miod Exp $	*/
/*	$NetBSD: fp_complete.c,v 1.5 2002/01/18 22:15:56 ross Exp $	*/

/*-
 * Copyright (c) 2001 Ross Harvey
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <machine/cpu.h>
#include <machine/fpu.h>
#include <machine/reg.h>
#include <alpha/alpha/db_instruction.h>

#include <lib/libkern/softfloat.h>

#define	TSWINSIZE 4	/* size of trap shadow window in u_int32_t units */

/*	Set Name		Opcodes			AARM C.* Symbols  */

#define	CPUREG_CLASS		(0xfUL << 0x10)		/* INT[ALSM]	  */
#define	FPUREG_CLASS		(0xfUL << 0x14)		/* ITFP, FLT[ILV] */
#define	CHECKFUNCTIONCODE	(1UL << 0x18)		/* MISC		  */
#define	TRAPSHADOWBOUNDARY	(1UL << 0x00 |		/* PAL		  */\
				 1UL << 0x19 |		/* \PAL\	  */\
				 1UL << 0x1a |		/* JSR		  */\
				 1UL << 0x1b |		/* \PAL\	  */\
				 1UL << 0x1d |		/* \PAL\	  */\
				 1UL << 0x1e |		/* \PAL\	  */\
				 1UL << 0x1f |		/* \PAL\	  */\
				 0xffffUL << 0x30 | 	/* branch ops	  */\
				 CHECKFUNCTIONCODE)

#define	MAKE_FLOATXX(width, expwidth, sign, exp, msb, rest_of_frac) \
	(u_int ## width ## _t)(sign) << ((width) - 1)			|\
	(u_int ## width ## _t)(exp)  << ((width) - 1 - (expwidth))	|\
	(u_int ## width ## _t)(msb)  << ((width) - 1 - (expwidth) - 1)	|\
	(u_int ## width ## _t)(rest_of_frac)

#define	FLOAT32QNAN MAKE_FLOATXX(32, 8, 0, 0xff, 1, 0)
#define	FLOAT64QNAN MAKE_FLOATXX(64, 11, 0, 0x7ff, 1, 0)

#define IS_SUBNORMAL(v)	((v)->exp == 0 && (v)->frac != 0)

#define	PREFILTER_SUBNORMAL(p,v) \
do { \
	if ((p)->p_md.md_flags & IEEE_MAP_DMZ && IS_SUBNORMAL(v)) \
		(v)->frac = 0; \
} while (0)

#define	POSTFILTER_SUBNORMAL(p,v) \
do { \
	if ((p)->p_md.md_flags & IEEE_MAP_UMZ && IS_SUBNORMAL(v)) \
		(v)->frac = 0; \
} while (0)

	/* Alpha returns 2.0 for true, all zeroes for false. */

#define CMP_RESULT(flag) ((flag) ? 4UL << 60 : 0L)

	/* Move bits from sw fp_c to hw fpcr. */

#define	CRBLIT(sw, hw, m, offs) (((sw) & ~(m)) | ((hw) >> (offs) & (m)))

/*
 * Temporary trap shadow instrumentation. The [un]resolved counters
 * could be kept permanently, as they provide information on whether
 * user code has met AARM trap shadow generation requirements.
 */

struct alpha_shadow {
	u_int64_t resolved;	/* cases trigger pc found */
	u_int64_t unresolved;	/* cases it wasn't, code problems? */
	u_int64_t scans;		/* trap shadow scans */
	u_int64_t len;		/* number of instructions examined */
	u_int64_t uop;		/* bit mask of unexpected opcodes */
	u_int64_t sqrts;	/* ev6+ square root single count */
	u_int64_t sqrtt;	/* ev6+ square root double count */
	u_int32_t ufunc;	/* bit mask of unexpected functions */
	u_int32_t max;		/* max trap shadow scan */
	u_int32_t nilswop;	/* unexpected op codes */
	u_int32_t nilswfunc;	/* unexpected function codes */
	u_int32_t nilanyop;	/* this "cannot happen" */
	u_int32_t vax;		/* sigs from vax fp opcodes */
} alpha_shadow, alpha_shadow_zero;

static float64 float64_unk(float64, float64);
static float64 compare_un(float64, float64);
static float64 compare_eq(float64, float64);
static float64 compare_lt(float64, float64);
static float64 compare_le(float64, float64);
static void cvt_qs_ts_st_gf_qf(u_int32_t, struct proc *);
static void cvt_gd(u_int32_t, struct proc *);
static void cvt_qt_dg_qg(u_int32_t, struct proc *);
static void cvt_tq_gq(u_int32_t, struct proc *);

static float32 (*swfp_s[])(float32, float32) = {
	float32_add, float32_sub, float32_mul, float32_div,
};

static float64 (*swfp_t[])(float64, float64) = {
	float64_add, float64_sub, float64_mul, float64_div,
	compare_un,    compare_eq,    compare_lt,    compare_le,
	float64_unk, float64_unk, float64_unk, float64_unk
};

static void (*swfp_cvt[])(u_int32_t, struct proc *) = {
	cvt_qs_ts_st_gf_qf, cvt_gd, cvt_qt_dg_qg, cvt_tq_gq
};

static void
this_cannot_happen(int what_cannot_happen, int64_t bits)
{
	static int total;
	alpha_instruction inst;
	static u_int64_t reported;

	inst.bits = bits;
	++alpha_shadow.nilswfunc;
	if (bits != -1)
		alpha_shadow.uop |= 1UL << inst.generic_format.opcode;
	if (1UL << what_cannot_happen & reported)
		return;
	reported |= 1UL << what_cannot_happen;
	if (total >= 1000)
		return;	/* right now, this return "cannot happen" */
	++total;
	if (bits)
		printf("FP instruction %x\n", (unsigned int)bits);
	printf("FP event %d/%lx/%lx\n", what_cannot_happen,
	    (unsigned long)reported, (unsigned long)alpha_shadow.uop);
}

static __inline void
sts(unsigned int rn, s_float *v, struct proc *p)
{
	alpha_sts(rn, v);
	PREFILTER_SUBNORMAL(p, v);
}

static __inline void
stt(unsigned int rn, t_float *v, struct proc *p)
{
	alpha_stt(rn, v);
	PREFILTER_SUBNORMAL(p, v);
}

static __inline void
lds(unsigned int rn, s_float *v, struct proc *p)
{
	POSTFILTER_SUBNORMAL(p, v);
	alpha_lds(rn, v);
}

static __inline void
ldt(unsigned int rn, t_float *v, struct proc *p)
{
	POSTFILTER_SUBNORMAL(p, v);
	alpha_ldt(rn, v);
}

static float64
compare_lt(float64 a, float64 b)
{
	return CMP_RESULT(float64_lt(a, b));
}

static float64
compare_le(float64 a, float64 b)
{
	return CMP_RESULT(float64_le(a, b));
}

static float64
compare_un(float64 a, float64 b)
{
	if (float64_is_nan(a) | float64_is_nan(b)) {
		if (float64_is_signaling_nan(a) | float64_is_signaling_nan(b))
			float_set_invalid();
		return CMP_RESULT(1);
	}
	return CMP_RESULT(0);
}

static float64
compare_eq(float64 a, float64 b)
{
	return CMP_RESULT(float64_eq(a, b));
}
/*
 * A note regarding the VAX FP ops.
 *
 * The AARM gives us complete leeway to set or not set status flags on VAX
 * ops, but we do any subnorm, NaN and dirty zero fixups anyway, and we set
 * flags by IEEE rules.  Many ops are common to d/f/g and s/t source types.
 * For the purely vax ones, it's hard to imagine ever running them.
 * (Generated VAX fp ops with completion flags? Hmm.)  We are careful never
 * to panic, assert, or print unlimited output based on a path through the
 * decoder, so weird cases don't become security issues.
 */
static void
cvt_qs_ts_st_gf_qf(u_int32_t inst_bits, struct proc *p)
{
	t_float tfb, tfc;
	s_float sfb, sfc;
	alpha_instruction inst;

	inst.bits = inst_bits;
	/*
	 * cvtst and cvtts have the same opcode, function, and source.  The
	 * distinction for cvtst is hidden in the illegal modifier combinations.
	 * We decode even the non-/s modifier, so that the fix-up-always mode
	 * works on ev6 and later. The rounding bits are unused and fixed for
	 * cvtst, so we check those too.
	 */
	switch(inst.float_format.function) {
	case op_cvtst:
	case op_cvtst_u:
		sts(inst.float_detail.fb, &sfb, p);
		tfc.i = float32_to_float64(sfb.i);
		ldt(inst.float_detail.fc, &tfc, p);
		return;
	}
	if(inst.float_detail.src == 2) {
		stt(inst.float_detail.fb, &tfb, p);
		sfc.i = float64_to_float32(tfb.i);
		lds(inst.float_detail.fc, &sfc, p);
		return;
	}
	/* 0: S/F */
	/* 1:  /D */
	/* 3: Q/Q */
	this_cannot_happen(5, inst.generic_format.opcode);
	tfc.i = FLOAT64QNAN;
	ldt(inst.float_detail.fc, &tfc, p);
	return;
}

static void
cvt_gd(u_int32_t inst_bits, struct proc *p)
{
	t_float tfb, tfc;
	alpha_instruction inst;

	inst.bits = inst_bits;
	stt(inst.float_detail.fb, &tfb, p);
	(void) float64_to_float32(tfb.i);
	p->p_md.md_flags &= ~OPENBSD_FLAG_TO_FP_C(FP_X_IMP);
	tfc.i = float64_add(tfb.i, (float64)0);
	ldt(inst.float_detail.fc, &tfc, p);
}

static void
cvt_qt_dg_qg(u_int32_t inst_bits, struct proc *p)
{
	t_float tfb, tfc;
	alpha_instruction inst;

	inst.bits = inst_bits;
	switch(inst.float_detail.src) {
	case 0:	/* S/F */
		this_cannot_happen(3, inst.bits);
		/* fall thru */
	case 1: /* D */
		/* VAX dirty 0's and reserved ops => UNPREDICTABLE */
		/* We've done what's important by just not trapping */
		tfc.i = 0;
		break;
	case 2: /* T/G */
		this_cannot_happen(4, inst.bits);
		tfc.i = 0;
		break;
	case 3:	/* Q/Q */
		stt(inst.float_detail.fb, &tfb, p);
		tfc.i = int64_to_float64(tfb.i);
		break;
	}
	alpha_ldt(inst.float_detail.fc, &tfc);
}
/*
 * XXX: AARM and 754 seem to disagree here, also, beware of softfloat's
 *      unfortunate habit of always returning the nontrapping result.
 * XXX: there are several apparent AARM/AAH disagreements, as well as
 *      the issue of trap handler pc and trapping results.
 * XXX: this function will work for signed and unsigned 64-bit integers.
 *      rounding will happen per IEEE 754.  invalid exception will be
 *      raised if argument is infinity, not-a-number or if it
 *      overflows/underflows.  zero will be returned, in this case.
 */
static void
cvt_tq_gq(u_int32_t inst_bits, struct proc *p)
{
	t_float tfb, tfc;
	alpha_instruction inst;

	inst.bits = inst_bits;
	stt(inst.float_detail.fb, &tfb, p);
	tfc.i = float64_to_int64_no_overflow(tfb.i);
	alpha_ldt(inst.float_detail.fc, &tfc);	/* yes, ldt */
}

static u_int64_t
fp_c_to_fpcr_1(u_int64_t fpcr, u_int64_t fp_c)
{
	u_int64_t disables;

	/*
	 * It's hard to arrange for conforming bit fields, because the FP_C
	 * and the FPCR are both architected, with specified (and relatively
	 * scrambled) bit numbers. Defining an internal unscrambled FP_C
	 * wouldn't help much, because every user exception requires the
	 * architected bit order in the sigcontext.
	 *
	 * Programs that fiddle with the fpcr exception bits (instead of fp_c)
	 * will lose, because those bits can be and usually are subsetted;
	 * the official home is in the fp_c. Furthermore, the kernel puts
	 * phony enables (it lies :-) in the fpcr in order to get control when
	 * it is necessary to initially set a sticky bit.
	 */

	fpcr &= FPCR_DYN(3);

	/*
	 * enable traps = case where flag bit is clear OR program wants a trap
	 * enables = ~flags | mask
	 * disables = ~(~flags | mask)
	 * disables = flags & ~mask. Thank you, Augustus De Morgan (1806-1871)
	 */
	disables = FP_C_TO_OPENBSD_FLAG(fp_c) & ~FP_C_TO_OPENBSD_MASK(fp_c);

	fpcr |= (disables & (FP_X_IMP | FP_X_UFL)) << (61 - 3);
	fpcr |= (disables & (FP_X_OFL | FP_X_DZ | FP_X_INV)) << (49 - 0);

#	if !(FP_X_INV == 1 && FP_X_DZ == 2 && FP_X_OFL == 4 &&		\
	    FP_X_UFL == 8 && FP_X_IMP == 16 && FP_X_IOV == 32 &&	\
	    FP_X_UFL << (61 - 3) == FPCR_UNFD &&			\
	    FP_X_IMP << (61 - 3) == FPCR_INED &&			\
	    FP_X_OFL << (49 - 0) == FPCR_OVFD)
#		error "Assertion failed"
	/*
	 * We don't care about the other built-in bit numbers because they
	 * have been architecturally specified.
	 */
#	endif

	fpcr |= fp_c & FP_C_MIRRORED << (FPCR_MIR_START - FP_C_MIR_START);
	fpcr |= (fp_c & IEEE_MAP_DMZ) << 36;
	if (fp_c & FP_C_MIRRORED)
		fpcr |= FPCR_SUM;
	if (fp_c & IEEE_MAP_UMZ)
		fpcr |= FPCR_UNDZ | FPCR_UNFD;
	fpcr |= (~fp_c & IEEE_TRAP_ENABLE_DNO) << 41;
	return fpcr;
}

static void
fp_c_to_fpcr(struct proc *p)
{
	alpha_write_fpcr(fp_c_to_fpcr_1(alpha_read_fpcr(), p->p_md.md_flags));
}

void
alpha_write_fp_c(struct proc *p, u_int64_t fp_c)
{
	u_int64_t md_flags;

	fp_c &= MDP_FP_C;
	md_flags = p->p_md.md_flags;
	if ((md_flags & MDP_FP_C) == fp_c)
		return;
	p->p_md.md_flags = (md_flags & ~MDP_FP_C) | fp_c;
	alpha_enable_fp(p, 1);
	alpha_pal_wrfen(1);
	fp_c_to_fpcr(p);
	alpha_pal_wrfen(0);
}

u_int64_t
alpha_read_fp_c(struct proc *p)
{
	/*
	 * A possibly desirable EV6-specific optimization would deviate from
	 * the Alpha Architecture spec and keep some FP_C bits in the FPCR,
	 * but in a transparent way. Some of the code for that would need to
	 * go right here.
	 */
	return p->p_md.md_flags & MDP_FP_C;
}

static float64
float64_unk(float64 a, float64 b)
{
	return 0;
}

/*
 * The real function field encodings for IEEE and VAX FP instructions.
 *
 * Since there is only one operand type field, the cvtXX instructions
 * require a variety of special cases, and these have to be analyzed as
 * they don't always fit into the field descriptions in AARM section I.
 *
 * Lots of staring at bits in the appendix shows what's really going on.
 *
 *	   |	       |
 * 15 14 13|12 11 10 09|08 07 06 05
 * --------======------============
 *  TRAP   : RND : SRC : FUNCTION  :
 *  0  0  0:. . .:. . . . . . . . . . . . Imprecise
 *  0  0  1|. . .:. . . . . . . . . . . ./U underflow enable (if FP output)
 *	   |				 /V overfloat enable (if int output)
 *  0  1  0:. . .:. . . . . . . . . . . ."Unsupported", but used for CVTST
 *  0  1  1|. . .:. . . . . . . . . . . . Unsupported
 *  1  0  0:. . .:. . . . . . . . . . . ./S software completion (VAX only)
 *  1  0  1|. . .:. . . . . . . . . . . ./SU
 *	   |				 /SV
 *  1  1  0:. . .:. . . . . . . . . . . ."Unsupported", but used for CVTST/S
 *  1  1  1|. . .:. . . . . . . . . . . ./SUI (if FP output)	(IEEE only)
 *	   |				 /SVI (if int output)   (IEEE only)
 *  S  I  UV: In other words: bits 15:13 are S:I:UV, except that _usually_
 *	   |  not all combinations are valid.
 *	   |	       |
 * 15 14 13|12 11 10 09|08 07 06 05
 * --------======------============
 *  TRAP   : RND : SRC : FUNCTION  :
 *	   | 0	0 . . . . . . . . . . . ./C Chopped
 *	   : 0	1 . . . . . . . . . . . ./M Minus Infinity
 *	   | 1	0 . . . . . . . . . . . .   Normal
 *	   : 1	1 . . . . . . . . . . . ./D Dynamic (in FPCR: Plus Infinity)
 *	   |	       |
 * 15 14 13|12 11 10 09|08 07 06 05
 * --------======------============
 *  TRAP   : RND : SRC : FUNCTION  :
 *		   0 0. . . . . . . . . . S/F
 *		   0 1. . . . . . . . . . -/D
 *		   1 0. . . . . . . . . . T/G
 *		   1 1. . . . . . . . . . Q/Q
 *	   |	       |
 * 15 14 13|12 11 10 09|08 07 06 05
 * --------======------============
 *  TRAP   : RND : SRC : FUNCTION  :
 *			 0  0  0  0 . . . addX
 *			 0  0  0  1 . . . subX
 *			 0  0  1  0 . . . mulX
 *			 0  0  1  1 . . . divX
 *			 0  1  0  0 . . . cmpXun
 *			 0  1  0  1 . . . cmpXeq
 *			 0  1  1  0 . . . cmpXlt
 *			 0  1  1  1 . . . cmpXle
 *			 1  0  0  0 . . . reserved
 *			 1  0  0  1 . . . reserved
 *			 1  0  1  0 . . . sqrt[fg] (op_fix, not exactly "vax")
 *			 1  0  1  1 . . . sqrt[st] (op_fix, not exactly "ieee")
 *			 1  1  0  0 . . . cvtXs/f (cvt[qt]s, cvtst(!), cvt[gq]f)
 *			 1  1  0  1 . . . cvtXd   (vax only)
 *			 1  1  1  0 . . . cvtXt/g (cvtqt, cvt[dq]g only)
 *			 1  1  1  1 . . . cvtXq/q (cvttq, cvtgq)
 *	   |	       |
 * 15 14 13|12 11 10 09|08 07 06 05	  the twilight zone
 * --------======------============
 *  TRAP   : RND : SRC : FUNCTION  :
 * /s /i /u  x  x  1  0  1  1  0  0 . . . cvtts, /siu only 0, 1, 5, 7
 *  0  1  0  1  0  1  0  1  1  0  0 . . . cvtst   (src == T (!)) 2ac NOT /S
 *  1  1  0  1  0  1  0  1  1  0  0 . . . cvtst/s (src == T (!)) 6ac
 *  x  0  x  x  x  x  0	 1  1  1  1 . . . cvttq/_ (src == T)
 */

static void
alpha_fp_interpret(struct proc *p, u_int64_t bits)
{
	s_float sfa, sfb, sfc;
	t_float tfa, tfb, tfc;
	alpha_instruction inst;

	inst.bits = bits;
	switch(inst.generic_format.opcode) {
	default:
		/* this "cannot happen" */
		this_cannot_happen(2, inst.bits);
		return;
	case op_any_float:
		if (inst.float_format.function == op_cvtql_sv ||
		    inst.float_format.function == op_cvtql_v) {
			alpha_stt(inst.float_detail.fb, &tfb);
			sfc.i = (int64_t)tfb.i >= 0L ? INT_MAX : INT_MIN;
			alpha_lds(inst.float_detail.fc, &sfc);
			float_raise(FP_X_INV);
		} else {
			++alpha_shadow.nilanyop;
			this_cannot_happen(3, inst.bits);
		}
		break;
	case op_vax_float:
		++alpha_shadow.vax;	/* fall thru */
	case op_ieee_float:
	case op_fix_float:
		switch(inst.float_detail.src) {
		case op_src_sf:
			sts(inst.float_detail.fb, &sfb, p);
			if (inst.float_detail.opclass == 10)
				sfc.i = float32_sqrt(sfb.i);
			else if (inst.float_detail.opclass & ~3) {
				this_cannot_happen(1, inst.bits);
				sfc.i = FLOAT32QNAN;
			} else {
				sts(inst.float_detail.fa, &sfa, p);
				sfc.i = (*swfp_s[inst.float_detail.opclass])(
				    sfa.i, sfb.i);
			}
			lds(inst.float_detail.fc, &sfc, p);
			break;
		case op_src_xd:
		case op_src_tg:
			if (inst.float_detail.opclass >= 12)
				(*swfp_cvt[inst.float_detail.opclass - 12])(
				    inst.bits, p);
			else {
				stt(inst.float_detail.fb, &tfb, p);
				if (inst.float_detail.opclass == 10)
					tfc.i = float64_sqrt(tfb.i);
				else {
					stt(inst.float_detail.fa, &tfa, p);
					tfc.i = (*swfp_t[inst.float_detail
					    .opclass])(tfa.i, tfb.i);
				}
				ldt(inst.float_detail.fc, &tfc, p);
			}
			break;
		case op_src_qq:
			float_raise(FP_X_IMP);
			break;
		}
	}
}

int
alpha_fp_complete_at(u_long trigger_pc, struct proc *p, u_int64_t *ucode)
{
	int needsig;
	alpha_instruction inst;
	u_int64_t rm, fpcr, orig_fpcr;
	u_int64_t orig_flags, new_flags, changed_flags, md_flags;

	if (__predict_false(copyinsn(NULL, (u_int32_t *)trigger_pc,
	    (u_int32_t *)&inst))) {
		this_cannot_happen(6, -1);
		return SIGSEGV;
	}
	alpha_enable_fp(p, 1);
	alpha_pal_wrfen(1);
	/*
	 * If necessary, lie about the dynamic rounding mode so emulation
	 * software need go to only one place for it, and so we don't have to
	 * lock any memory locations or pass a third parameter to every
	 * SoftFloat entry point.
	 */
	orig_fpcr = fpcr = alpha_read_fpcr();
	rm = inst.float_detail.rnd;
	if (__predict_false(rm != 3 /* dynamic */ && rm != (fpcr >> 58 & 3))) {
		fpcr = (fpcr & ~FPCR_DYN(3)) | FPCR_DYN(rm);
		alpha_write_fpcr(fpcr);
	}
	orig_flags = FP_C_TO_OPENBSD_FLAG(p->p_md.md_flags);

	alpha_fp_interpret(p, inst.bits);

	md_flags = p->p_md.md_flags;

	new_flags = FP_C_TO_OPENBSD_FLAG(md_flags);
	changed_flags = orig_flags ^ new_flags;
	KASSERT((orig_flags | changed_flags) == new_flags); /* panic on 1->0 */
	alpha_write_fpcr(fp_c_to_fpcr_1(orig_fpcr, md_flags));
	needsig = changed_flags & FP_C_TO_OPENBSD_MASK(md_flags);
	alpha_pal_wrfen(0);
	if (__predict_false(needsig)) {
		*ucode = needsig;
		return SIGFPE;
	}
	return 0;
}

int
alpha_fp_complete(u_long a0, u_long a1, struct proc *p, u_int64_t *ucode)
{
	int t;
	int sig;
	u_int64_t op_class;
	alpha_instruction inst;
	/* "trigger_pc" is Compaq's term for the earliest faulting op */
	u_long trigger_pc, usertrap_pc;
	alpha_instruction *pc, *win_begin, tsw[TSWINSIZE];

	sig = SIGFPE;
	pc = (alpha_instruction *)p->p_md.md_tf->tf_regs[FRAME_PC];
	trigger_pc = (u_long)pc - 4;	/* for ALPHA_AMASK_PAT case */
	if (cpu_amask & ALPHA_AMASK_PAT) {
		if (a0 & 1 || alpha_fp_sync_complete) {
			sig = alpha_fp_complete_at(trigger_pc, p, ucode);
			goto done;
		}
	}
	*ucode = a0;
	if (!(a0 & 1))
		return sig;
/*
 * At this point we are somewhere in the trap shadow of one or more instruc-
 * tions that have trapped with software completion specified.  We have a mask
 * of the registers written by trapping instructions.
 *
 * Now step backwards through the trap shadow, clearing bits in the
 * destination write mask until the trigger instruction is found, and
 * interpret this one instruction in SW. If a SIGFPE is not required, back up
 * the PC until just after this instruction and restart. This will execute all
 * trap shadow instructions between the trigger pc and the trap pc twice.
 */
	trigger_pc = 0;
	win_begin = pc;
	++alpha_shadow.scans;
	t = alpha_shadow.len;
	for (--pc; a1; --pc) {
		++alpha_shadow.len;
		if (pc < win_begin) {
			win_begin = pc - TSWINSIZE + 1;
			if (_copyin(win_begin, tsw, sizeof tsw)) {
				/* sigh, try to get just one */
				win_begin = pc;
				if (_copyin(win_begin, tsw, 4))
					return SIGSEGV;
			}
		}
		inst = tsw[pc - win_begin];
		op_class = 1UL << inst.generic_format.opcode;
		if (op_class & FPUREG_CLASS) {
			a1 &= ~(1UL << (inst.operate_generic_format.rc + 32));
			trigger_pc = (u_long)pc;
		} else if (op_class & CPUREG_CLASS) {
			a1 &= ~(1UL << inst.operate_generic_format.rc);
			trigger_pc = (u_long)pc;
		} else if (op_class & TRAPSHADOWBOUNDARY) {
			if (op_class & CHECKFUNCTIONCODE) {
				if (inst.mem_format.displacement == op_trapb ||
				    inst.mem_format.displacement == op_excb)
					break;	/* code breaks AARM rules */
			} else
				break; /* code breaks AARM rules */
		}
		/* Some shadow-safe op, probably load, store, or FPTI class */
	}
	t = alpha_shadow.len - t;
	if (t > alpha_shadow.max)
		alpha_shadow.max = t;
	if (__predict_true(trigger_pc != 0 && a1 == 0)) {
		++alpha_shadow.resolved;
		sig = alpha_fp_complete_at(trigger_pc, p, ucode);
	} else {
		++alpha_shadow.unresolved;
		return sig;
	}
done:
	if (sig) {
		usertrap_pc = trigger_pc + 4;
		p->p_md.md_tf->tf_regs[FRAME_PC] = usertrap_pc;
		return sig;
	}
	return 0;
}
