/* ieee754-df.S double-precision floating point support for ARM

   Copyright (C) 2003, 2004, 2005  Free Software Foundation, Inc.
   Contributed by Nicolas Pitre (nico@cam.org)

   This file is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combine
   executable.)

   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/*
 * Notes: 
 * 
 * The goal of this code is to be as fast as possible.  This is
 * not meant to be easy to understand for the casual reader.
 * For slightly simpler code please see the single precision version
 * of this file.
 * 
 * Only the default rounding mode is intended for best performances.
 * Exceptions aren't supported yet, but that can be added quite easily
 * if necessary without impacting performances.
 */


@ For FPA, float words are always big-endian.
@ For VFP, floats words follow the memory system mode.
#if defined(__VFP_FP__) && !defined(__ARMEB__)
#define xl r0
#define xh r1
#define yl r2
#define yh r3
#else
#define xh r0
#define xl r1
#define yh r2
#define yl r3
#endif


#ifdef L_negdf2

ARM_FUNC_START negdf2
ARM_FUNC_ALIAS aeabi_dneg negdf2

	@ flip sign bit
	eor	xh, xh, #0x80000000
	RET

	FUNC_END aeabi_dneg
	FUNC_END negdf2

#endif

#ifdef L_addsubdf3

ARM_FUNC_START aeabi_drsub

	eor	xh, xh, #0x80000000	@ flip sign bit of first arg
	b	1f	

ARM_FUNC_START subdf3
ARM_FUNC_ALIAS aeabi_dsub subdf3

	eor	yh, yh, #0x80000000	@ flip sign bit of second arg
#if defined(__INTERWORKING_STUBS__)
	b	1f			@ Skip Thumb-code prologue
#endif

ARM_FUNC_START adddf3
ARM_FUNC_ALIAS aeabi_dadd adddf3

1:	stmfd	sp!, {r4, r5, lr}

	@ Look for zeroes, equal values, INF, or NAN.
	mov	r4, xh, lsl #1
	mov	r5, yh, lsl #1
	teq	r4, r5
	teqeq	xl, yl
	orrnes	ip, r4, xl
	orrnes	ip, r5, yl
	mvnnes	ip, r4, asr #21
	mvnnes	ip, r5, asr #21
	beq	LSYM(Lad_s)

	@ Compute exponent difference.  Make largest exponent in r4,
	@ corresponding arg in xh-xl, and positive exponent difference in r5.
	mov	r4, r4, lsr #21
	rsbs	r5, r4, r5, lsr #21
	rsblt	r5, r5, #0
	ble	1f
	add	r4, r4, r5
	eor	yl, xl, yl
	eor	yh, xh, yh
	eor	xl, yl, xl
	eor	xh, yh, xh
	eor	yl, xl, yl
	eor	yh, xh, yh
1:
	@ If exponent difference is too large, return largest argument
	@ already in xh-xl.  We need up to 54 bit to handle proper rounding
	@ of 0x1p54 - 1.1.
	cmp	r5, #54
	RETLDM	"r4, r5" hi

	@ Convert mantissa to signed integer.
	tst	xh, #0x80000000
	mov	xh, xh, lsl #12
	mov	ip, #0x00100000
	orr	xh, ip, xh, lsr #12
	beq	1f
	rsbs	xl, xl, #0
	rsc	xh, xh, #0
1:
	tst	yh, #0x80000000
	mov	yh, yh, lsl #12
	orr	yh, ip, yh, lsr #12
	beq	1f
	rsbs	yl, yl, #0
	rsc	yh, yh, #0
1:
	@ If exponent == difference, one or both args were denormalized.
	@ Since this is not common case, rescale them off line.
	teq	r4, r5
	beq	LSYM(Lad_d)
LSYM(Lad_x):

	@ Compensate for the exponent overlapping the mantissa MSB added later
	sub	r4, r4, #1

	@ Shift yh-yl right per r5, add to xh-xl, keep leftover bits into ip.
	rsbs	lr, r5, #32
	blt	1f
	mov	ip, yl, lsl lr
	adds	xl, xl, yl, lsr r5
	adc	xh, xh, #0
	adds	xl, xl, yh, lsl lr
	adcs	xh, xh, yh, asr r5
	b	2f
1:	sub	r5, r5, #32
	add	lr, lr, #32
	cmp	yl, #1
	mov	ip, yh, lsl lr
	orrcs	ip, ip, #2		@ 2 not 1, to allow lsr #1 later
	adds	xl, xl, yh, asr r5
	adcs	xh, xh, yh, asr #31
2:
	@ We now have a result in xh-xl-ip.
	@ Keep absolute value in xh-xl-ip, sign in r5 (the n bit was set above)
	and	r5, xh, #0x80000000
	bpl	LSYM(Lad_p)
	rsbs	ip, ip, #0
	rscs	xl, xl, #0
	rsc	xh, xh, #0

	@ Determine how to normalize the result.
LSYM(Lad_p):
	cmp	xh, #0x00100000
	bcc	LSYM(Lad_a)
	cmp	xh, #0x00200000
	bcc	LSYM(Lad_e)

	@ Result needs to be shifted right.
	movs	xh, xh, lsr #1
	movs	xl, xl, rrx
	mov	ip, ip, rrx
	add	r4, r4, #1

	@ Make sure we did not bust our exponent.
	mov	r2, r4, lsl #21
	cmn	r2, #(2 << 21)
	bcs	LSYM(Lad_o)

	@ Our result is now properly aligned into xh-xl, remaining bits in ip.
	@ Round with MSB of ip. If halfway between two numbers, round towards
	@ LSB of xl = 0.
	@ Pack final result together.
LSYM(Lad_e):
	cmp	ip, #0x80000000
	moveqs	ip, xl, lsr #1
	adcs	xl, xl, #0
	adc	xh, xh, r4, lsl #20
	orr	xh, xh, r5
	RETLDM	"r4, r5"

	@ Result must be shifted left and exponent adjusted.
LSYM(Lad_a):
	movs	ip, ip, lsl #1
	adcs	xl, xl, xl
	adc	xh, xh, xh
	tst	xh, #0x00100000
	sub	r4, r4, #1
	bne	LSYM(Lad_e)

	@ No rounding necessary since ip will always be 0 at this point.
LSYM(Lad_l):

#if __ARM_ARCH__ < 5

	teq	xh, #0
	movne	r3, #20
	moveq	r3, #52
	moveq	xh, xl
	moveq	xl, #0
	mov	r2, xh
	cmp	r2, #(1 << 16)
	movhs	r2, r2, lsr #16
	subhs	r3, r3, #16
	cmp	r2, #(1 << 8)
	movhs	r2, r2, lsr #8
	subhs	r3, r3, #8
	cmp	r2, #(1 << 4)
	movhs	r2, r2, lsr #4
	subhs	r3, r3, #4
	cmp	r2, #(1 << 2)
	subhs	r3, r3, #2
	sublo	r3, r3, r2, lsr #1
	sub	r3, r3, r2, lsr #3

#else

	teq	xh, #0
	moveq	xh, xl
	moveq	xl, #0
	clz	r3, xh
	addeq	r3, r3, #32
	sub	r3, r3, #11

#endif

	@ determine how to shift the value.
	subs	r2, r3, #32
	bge	2f
	adds	r2, r2, #12
	ble	1f

	@ shift value left 21 to 31 bits, or actually right 11 to 1 bits
	@ since a register switch happened above.
	add	ip, r2, #20
	rsb	r2, r2, #12
	mov	xl, xh, lsl ip
	mov	xh, xh, lsr r2
	b	3f

	@ actually shift value left 1 to 20 bits, which might also represent
	@ 32 to 52 bits if counting the register switch that happened earlier.
1:	add	r2, r2, #20
2:	rsble	ip, r2, #32
	mov	xh, xh, lsl r2
	orrle	xh, xh, xl, lsr ip
	movle	xl, xl, lsl r2

	@ adjust exponent accordingly.
3:	subs	r4, r4, r3
	addge	xh, xh, r4, lsl #20
	orrge	xh, xh, r5
	RETLDM	"r4, r5" ge

	@ Exponent too small, denormalize result.
	@ Find out proper shift value.
	mvn	r4, r4
	subs	r4, r4, #31
	bge	2f
	adds	r4, r4, #12
	bgt	1f

	@ shift result right of 1 to 20 bits, sign is in r5.
	add	r4, r4, #20
	rsb	r2, r4, #32
	mov	xl, xl, lsr r4
	orr	xl, xl, xh, lsl r2
	orr	xh, r5, xh, lsr r4
	RETLDM	"r4, r5"

	@ shift result right of 21 to 31 bits, or left 11 to 1 bits after
	@ a register switch from xh to xl.
1:	rsb	r4, r4, #12
	rsb	r2, r4, #32
	mov	xl, xl, lsr r2
	orr	xl, xl, xh, lsl r4
	mov	xh, r5
	RETLDM	"r4, r5"

	@ Shift value right of 32 to 64 bits, or 0 to 32 bits after a switch
	@ from xh to xl.
2:	mov	xl, xh, lsr r4
	mov	xh, r5
	RETLDM	"r4, r5"

	@ Adjust exponents for denormalized arguments.
	@ Note that r4 must not remain equal to 0.
LSYM(Lad_d):
	teq	r4, #0
	eor	yh, yh, #0x00100000
	eoreq	xh, xh, #0x00100000
	addeq	r4, r4, #1
	subne	r5, r5, #1
	b	LSYM(Lad_x)


LSYM(Lad_s):
	mvns	ip, r4, asr #21
	mvnnes	ip, r5, asr #21
	beq	LSYM(Lad_i)

	teq	r4, r5
	teqeq	xl, yl
	beq	1f

	@ Result is x + 0.0 = x or 0.0 + y = y.
	orrs	ip, r4, xl
	moveq	xh, yh
	moveq	xl, yl
	RETLDM	"r4, r5"

1:	teq	xh, yh

	@ Result is x - x = 0.
	movne	xh, #0
	movne	xl, #0
	RETLDM	"r4, r5" ne

	@ Result is x + x = 2x.
	movs	ip, r4, lsr #21
	bne	2f
	movs	xl, xl, lsl #1
	adcs	xh, xh, xh
	orrcs	xh, xh, #0x80000000
	RETLDM	"r4, r5"
2:	adds	r4, r4, #(2 << 21)
	addcc	xh, xh, #(1 << 20)
	RETLDM	"r4, r5" cc
	and	r5, xh, #0x80000000

	@ Overflow: return INF.
LSYM(Lad_o):
	orr	xh, r5, #0x7f000000
	orr	xh, xh, #0x00f00000
	mov	xl, #0
	RETLDM	"r4, r5"

	@ At least one of x or y is INF/NAN.
	@   if xh-xl != INF/NAN: return yh-yl (which is INF/NAN)
	@   if yh-yl != INF/NAN: return xh-xl (which is INF/NAN)
	@   if either is NAN: return NAN
	@   if opposite sign: return NAN
	@   otherwise return xh-xl (which is INF or -INF)
LSYM(Lad_i):
	mvns	ip, r4, asr #21
	movne	xh, yh
	movne	xl, yl
	mvneqs	ip, r5, asr #21
	movne	yh, xh
	movne	yl, xl
	orrs	r4, xl, xh, lsl #12
	orreqs	r5, yl, yh, lsl #12
	teqeq	xh, yh
	orrne	xh, xh, #0x00080000	@ quiet NAN
	RETLDM	"r4, r5"

	FUNC_END aeabi_dsub
	FUNC_END subdf3
	FUNC_END aeabi_dadd
	FUNC_END adddf3

ARM_FUNC_START floatunsidf
ARM_FUNC_ALIAS aeabi_ui2d floatunsidf

	teq	r0, #0
	moveq	r1, #0
	RETc(eq)
	stmfd	sp!, {r4, r5, lr}
	mov	r4, #0x400		@ initial exponent
	add	r4, r4, #(52-1 - 1)
	mov	r5, #0			@ sign bit is 0
	.ifnc	xl, r0
	mov	xl, r0
	.endif
	mov	xh, #0
	b	LSYM(Lad_l)

	FUNC_END aeabi_ui2d
	FUNC_END floatunsidf

ARM_FUNC_START floatsidf
ARM_FUNC_ALIAS aeabi_i2d floatsidf

	teq	r0, #0
	moveq	r1, #0
	RETc(eq)
	stmfd	sp!, {r4, r5, lr}
	mov	r4, #0x400		@ initial exponent
	add	r4, r4, #(52-1 - 1)
	ands	r5, r0, #0x80000000	@ sign bit in r5
	rsbmi	r0, r0, #0		@ absolute value
	.ifnc	xl, r0
	mov	xl, r0
	.endif
	mov	xh, #0
	b	LSYM(Lad_l)

	FUNC_END aeabi_i2d
	FUNC_END floatsidf

ARM_FUNC_START extendsfdf2
ARM_FUNC_ALIAS aeabi_f2d extendsfdf2

	movs	r2, r0, lsl #1		@ toss sign bit
	mov	xh, r2, asr #3		@ stretch exponent
	mov	xh, xh, rrx		@ retrieve sign bit
	mov	xl, r2, lsl #28		@ retrieve remaining bits
	andnes	r3, r2, #0xff000000	@ isolate exponent
	teqne	r3, #0xff000000		@ if not 0, check if INF or NAN
	eorne	xh, xh, #0x38000000	@ fixup exponent otherwise.
	RETc(ne)			@ and return it.

	teq	r2, #0			@ if actually 0
	teqne	r3, #0xff000000		@ or INF or NAN
	RETc(eq)			@ we are done already.

	@ value was denormalized.  We can normalize it now.
	stmfd	sp!, {r4, r5, lr}
	mov	r4, #0x380		@ setup corresponding exponent
	and	r5, xh, #0x80000000	@ move sign bit in r5
	bic	xh, xh, #0x80000000
	b	LSYM(Lad_l)

	FUNC_END aeabi_f2d
	FUNC_END extendsfdf2

ARM_FUNC_START floatundidf
ARM_FUNC_ALIAS aeabi_ul2d floatundidf

	orrs	r2, r0, r1
#if !defined (__VFP_FP__) && !defined(__SOFTFP__)
	mvfeqd	f0, #0.0
#endif
	RETc(eq)

#if !defined (__VFP_FP__) && !defined(__SOFTFP__)
	@ For hard FPA code we want to return via the tail below so that
	@ we can return the result in f0 as well as in r0/r1 for backwards
	@ compatibility.
	adr	ip, LSYM(f0_ret)
	stmfd	sp!, {r4, r5, ip, lr}
#else
	stmfd	sp!, {r4, r5, lr}
#endif

	mov	r5, #0
	b	2f

ARM_FUNC_START floatdidf
ARM_FUNC_ALIAS aeabi_l2d floatdidf

	orrs	r2, r0, r1
#if !defined (__VFP_FP__) && !defined(__SOFTFP__)
	mvfeqd	f0, #0.0
#endif
	RETc(eq)

#if !defined (__VFP_FP__) && !defined(__SOFTFP__)
	@ For hard FPA code we want to return via the tail below so that
	@ we can return the result in f0 as well as in r0/r1 for backwards
	@ compatibility.
	adr	ip, LSYM(f0_ret)
	stmfd	sp!, {r4, r5, ip, lr}
#else
	stmfd	sp!, {r4, r5, lr}
#endif

	ands	r5, ah, #0x80000000	@ sign bit in r5
	bpl	2f
	rsbs	al, al, #0
	rsc	ah, ah, #0
2:
	mov	r4, #0x400		@ initial exponent
	add	r4, r4, #(52-1 - 1)

	@ FPA little-endian: must swap the word order.
	.ifnc	xh, ah
	mov	ip, al
	mov	xh, ah
	mov	xl, ip
	.endif

	movs	ip, xh, lsr #22
	beq	LSYM(Lad_p)

	@ The value is too big.  Scale it down a bit...
	mov	r2, #3
	movs	ip, ip, lsr #3
	addne	r2, r2, #3
	movs	ip, ip, lsr #3
	addne	r2, r2, #3
	add	r2, r2, ip, lsr #3

	rsb	r3, r2, #32
	mov	ip, xl, lsl r3
	mov	xl, xl, lsr r2
	orr	xl, xl, xh, lsl r3
	mov	xh, xh, lsr r2
	add	r4, r4, r2
	b	LSYM(Lad_p)

#if !defined (__VFP_FP__) && !defined(__SOFTFP__)

	@ Legacy code expects the result to be returned in f0.  Copy it
	@ there as well.
LSYM(f0_ret):
	stmfd	sp!, {r0, r1}
	ldfd	f0, [sp], #8
	RETLDM

#endif

	FUNC_END floatdidf
	FUNC_END aeabi_l2d
	FUNC_END floatundidf
	FUNC_END aeabi_ul2d

#endif /* L_addsubdf3 */

#ifdef L_muldivdf3

ARM_FUNC_START muldf3
ARM_FUNC_ALIAS aeabi_dmul muldf3
	stmfd	sp!, {r4, r5, r6, lr}

	@ Mask out exponents, trap any zero/denormal/INF/NAN.
	mov	ip, #0xff
	orr	ip, ip, #0x700
	ands	r4, ip, xh, lsr #20
	andnes	r5, ip, yh, lsr #20
	teqne	r4, ip
	teqne	r5, ip
	bleq	LSYM(Lml_s)

	@ Add exponents together
	add	r4, r4, r5

	@ Determine final sign.
	eor	r6, xh, yh

	@ Convert mantissa to unsigned integer.
	@ If power of two, branch to a separate path.
	bic	xh, xh, ip, lsl #21
	bic	yh, yh, ip, lsl #21
	orrs	r5, xl, xh, lsl #12
	orrnes	r5, yl, yh, lsl #12
	orr	xh, xh, #0x00100000
	orr	yh, yh, #0x00100000
	beq	LSYM(Lml_1)

#if __ARM_ARCH__ < 4

	@ Put sign bit in r6, which will be restored in yl later.
	and   r6, r6, #0x80000000

	@ Well, no way to make it shorter without the umull instruction.
	stmfd	sp!, {r6, r7, r8, r9, sl, fp}
	mov	r7, xl, lsr #16
	mov	r8, yl, lsr #16
	mov	r9, xh, lsr #16
	mov	sl, yh, lsr #16
	bic	xl, xl, r7, lsl #16
	bic	yl, yl, r8, lsl #16
	bic	xh, xh, r9, lsl #16
	bic	yh, yh, sl, lsl #16
	mul	ip, xl, yl
	mul	fp, xl, r8
	mov	lr, #0
	adds	ip, ip, fp, lsl #16
	adc	lr, lr, fp, lsr #16
	mul	fp, r7, yl
	adds	ip, ip, fp, lsl #16
	adc	lr, lr, fp, lsr #16
	mul	fp, xl, sl
	mov	r5, #0
	adds	lr, lr, fp, lsl #16
	adc	r5, r5, fp, lsr #16
	mul	fp, r7, yh
	adds	lr, lr, fp, lsl #16
	adc	r5, r5, fp, lsr #16
	mul	fp, xh, r8
	adds	lr, lr, fp, lsl #16
	adc	r5, r5, fp, lsr #16
	mul	fp, r9, yl
	adds	lr, lr, fp, lsl #16
	adc	r5, r5, fp, lsr #16
	mul	fp, xh, sl
	mul	r6, r9, sl
	adds	r5, r5, fp, lsl #16
	adc	r6, r6, fp, lsr #16
	mul	fp, r9, yh
	adds	r5, r5, fp, lsl #16
	adc	r6, r6, fp, lsr #16
	mul	fp, xl, yh
	adds	lr, lr, fp
	mul	fp, r7, sl
	adcs	r5, r5, fp
	mul	fp, xh, yl
	adc	r6, r6, #0
	adds	lr, lr, fp
	mul	fp, r9, r8
	adcs	r5, r5, fp
	mul	fp, r7, r8
	adc	r6, r6, #0
	adds	lr, lr, fp
	mul	fp, xh, yh
	adcs	r5, r5, fp
	adc	r6, r6, #0
	ldmfd	sp!, {yl, r7, r8, r9, sl, fp}

#else

	@ Here is the actual multiplication.
	umull	ip, lr, xl, yl
	mov	r5, #0
	umlal	lr, r5, xh, yl
	and	yl, r6, #0x80000000
	umlal	lr, r5, xl, yh
	mov	r6, #0
	umlal	r5, r6, xh, yh

#endif

	@ The LSBs in ip are only significant for the final rounding.
	@ Fold them into lr.
	teq	ip, #0
	orrne	lr, lr, #1

	@ Adjust result upon the MSB position.
	sub	r4, r4, #0xff
	cmp	r6, #(1 << (20-11))
	sbc	r4, r4, #0x300
	bcs	1f
	movs	lr, lr, lsl #1
	adcs	r5, r5, r5
	adc	r6, r6, r6
1:
	@ Shift to final position, add sign to result.
	orr	xh, yl, r6, lsl #11
	orr	xh, xh, r5, lsr #21
	mov	xl, r5, lsl #11
	orr	xl, xl, lr, lsr #21
	mov	lr, lr, lsl #11

	@ Check exponent range for under/overflow.
	subs	ip, r4, #(254 - 1)
	cmphi	ip, #0x700
	bhi	LSYM(Lml_u)

	@ Round the result, merge final exponent.
	cmp	lr, #0x80000000
	moveqs	lr, xl, lsr #1
	adcs	xl, xl, #0
	adc	xh, xh, r4, lsl #20
	RETLDM	"r4, r5, r6"

	@ Multiplication by 0x1p*: let''s shortcut a lot of code.
LSYM(Lml_1):
	and	r6, r6, #0x80000000
	orr	xh, r6, xh
	orr	xl, xl, yl
	eor	xh, xh, yh
	subs	r4, r4, ip, lsr #1
	rsbgts	r5, r4, ip
	orrgt	xh, xh, r4, lsl #20
	RETLDM	"r4, r5, r6" gt

	@ Under/overflow: fix things up for the code below.
	orr	xh, xh, #0x00100000
	mov	lr, #0
	subs	r4, r4, #1

LSYM(Lml_u):
	@ Overflow?
	bgt	LSYM(Lml_o)

	@ Check if denormalized result is possible, otherwise return signed 0.
	cmn	r4, #(53 + 1)
	movle	xl, #0
	bicle	xh, xh, #0x7fffffff
	RETLDM	"r4, r5, r6" le

	@ Find out proper shift value.
	rsb	r4, r4, #0
	subs	r4, r4, #32
	bge	2f
	adds	r4, r4, #12
	bgt	1f

	@ shift result right of 1 to 20 bits, preserve sign bit, round, etc.
	add	r4, r4, #20
	rsb	r5, r4, #32
	mov	r3, xl, lsl r5
	mov	xl, xl, lsr r4
	orr	xl, xl, xh, lsl r5
	and	r2, xh, #0x80000000
	bic	xh, xh, #0x80000000
	adds	xl, xl, r3, lsr #31
	adc	xh, r2, xh, lsr r4
	orrs	lr, lr, r3, lsl #1
	biceq	xl, xl, r3, lsr #31
	RETLDM	"r4, r5, r6"

	@ shift result right of 21 to 31 bits, or left 11 to 1 bits after
	@ a register switch from xh to xl. Then round.
1:	rsb	r4, r4, #12
	rsb	r5, r4, #32
	mov	r3, xl, lsl r4
	mov	xl, xl, lsr r5
	orr	xl, xl, xh, lsl r4
	bic	xh, xh, #0x7fffffff
	adds	xl, xl, r3, lsr #31
	adc	xh, xh, #0
	orrs	lr, lr, r3, lsl #1
	biceq	xl, xl, r3, lsr #31
	RETLDM	"r4, r5, r6"

	@ Shift value right of 32 to 64 bits, or 0 to 32 bits after a switch
	@ from xh to xl.  Leftover bits are in r3-r6-lr for rounding.
2:	rsb	r5, r4, #32
	orr	lr, lr, xl, lsl r5
	mov	r3, xl, lsr r4
	orr	r3, r3, xh, lsl r5
	mov	xl, xh, lsr r4
	bic	xh, xh, #0x7fffffff
	bic	xl, xl, xh, lsr r4
	add	xl, xl, r3, lsr #31
	orrs	lr, lr, r3, lsl #1
	biceq	xl, xl, r3, lsr #31
	RETLDM	"r4, r5, r6"

	@ One or both arguments are denormalized.
	@ Scale them leftwards and preserve sign bit.
LSYM(Lml_d):
	teq	r4, #0
	bne	2f
	and	r6, xh, #0x80000000
1:	movs	xl, xl, lsl #1
	adc	xh, xh, xh
	tst	xh, #0x00100000
	subeq	r4, r4, #1
	beq	1b
	orr	xh, xh, r6
	teq	r5, #0
	movne	pc, lr
2:	and	r6, yh, #0x80000000
3:	movs	yl, yl, lsl #1
	adc	yh, yh, yh
	tst	yh, #0x00100000
	subeq	r5, r5, #1
	beq	3b
	orr	yh, yh, r6
	mov	pc, lr

LSYM(Lml_s):
	@ Isolate the INF and NAN cases away
	teq	r4, ip
	and	r5, ip, yh, lsr #20
	teqne	r5, ip
	beq	1f

	@ Here, one or more arguments are either denormalized or zero.
	orrs	r6, xl, xh, lsl #1
	orrnes	r6, yl, yh, lsl #1
	bne	LSYM(Lml_d)

	@ Result is 0, but determine sign anyway.
LSYM(Lml_z):
	eor	xh, xh, yh
	bic	xh, xh, #0x7fffffff
	mov	xl, #0
	RETLDM	"r4, r5, r6"

1:	@ One or both args are INF or NAN.
	orrs	r6, xl, xh, lsl #1
	moveq	xl, yl
	moveq	xh, yh
	orrnes	r6, yl, yh, lsl #1
	beq	LSYM(Lml_n)		@ 0 * INF or INF * 0 -> NAN
	teq	r4, ip
	bne	1f
	orrs	r6, xl, xh, lsl #12
	bne	LSYM(Lml_n)		@ NAN * <anything> -> NAN
1:	teq	r5, ip
	bne	LSYM(Lml_i)
	orrs	r6, yl, yh, lsl #12
	movne	xl, yl
	movne	xh, yh
	bne	LSYM(Lml_n)		@ <anything> * NAN -> NAN

	@ Result is INF, but we need to determine its sign.
LSYM(Lml_i):
	eor	xh, xh, yh

	@ Overflow: return INF (sign already in xh).
LSYM(Lml_o):
	and	xh, xh, #0x80000000
	orr	xh, xh, #0x7f000000
	orr	xh, xh, #0x00f00000
	mov	xl, #0
	RETLDM	"r4, r5, r6"

	@ Return a quiet NAN.
LSYM(Lml_n):
	orr	xh, xh, #0x7f000000
	orr	xh, xh, #0x00f80000
	RETLDM	"r4, r5, r6"

	FUNC_END aeabi_dmul
	FUNC_END muldf3

ARM_FUNC_START divdf3
ARM_FUNC_ALIAS aeabi_ddiv divdf3
	
	stmfd	sp!, {r4, r5, r6, lr}

	@ Mask out exponents, trap any zero/denormal/INF/NAN.
	mov	ip, #0xff
	orr	ip, ip, #0x700
	ands	r4, ip, xh, lsr #20
	andnes	r5, ip, yh, lsr #20
	teqne	r4, ip
	teqne	r5, ip
	bleq	LSYM(Ldv_s)

	@ Substract divisor exponent from dividend''s.
	sub	r4, r4, r5

	@ Preserve final sign into lr.
	eor	lr, xh, yh

	@ Convert mantissa to unsigned integer.
	@ Dividend -> r5-r6, divisor -> yh-yl.
	orrs	r5, yl, yh, lsl #12
	mov	xh, xh, lsl #12
	beq	LSYM(Ldv_1)
	mov	yh, yh, lsl #12
	mov	r5, #0x10000000
	orr	yh, r5, yh, lsr #4
	orr	yh, yh, yl, lsr #24
	mov	yl, yl, lsl #8
	orr	r5, r5, xh, lsr #4
	orr	r5, r5, xl, lsr #24
	mov	r6, xl, lsl #8

	@ Initialize xh with final sign bit.
	and	xh, lr, #0x80000000

	@ Ensure result will land to known bit position.
	@ Apply exponent bias accordingly.
	cmp	r5, yh
	cmpeq	r6, yl
	adc	r4, r4, #(255 - 2)
	add	r4, r4, #0x300
	bcs	1f
	movs	yh, yh, lsr #1
	mov	yl, yl, rrx
1:
	@ Perform first substraction to align result to a nibble.
	subs	r6, r6, yl
	sbc	r5, r5, yh
	movs	yh, yh, lsr #1
	mov	yl, yl, rrx
	mov	xl, #0x00100000
	mov	ip, #0x00080000

	@ The actual division loop.
1:	subs	lr, r6, yl
	sbcs	lr, r5, yh
	subcs	r6, r6, yl
	movcs	r5, lr
	orrcs	xl, xl, ip
	movs	yh, yh, lsr #1
	mov	yl, yl, rrx
	subs	lr, r6, yl
	sbcs	lr, r5, yh
	subcs	r6, r6, yl
	movcs	r5, lr
	orrcs	xl, xl, ip, lsr #1
	movs	yh, yh, lsr #1
	mov	yl, yl, rrx
	subs	lr, r6, yl
	sbcs	lr, r5, yh
	subcs	r6, r6, yl
	movcs	r5, lr
	orrcs	xl, xl, ip, lsr #2
	movs	yh, yh, lsr #1
	mov	yl, yl, rrx
	subs	lr, r6, yl
	sbcs	lr, r5, yh
	subcs	r6, r6, yl
	movcs	r5, lr
	orrcs	xl, xl, ip, lsr #3

	orrs	lr, r5, r6
	beq	2f
	mov	r5, r5, lsl #4
	orr	r5, r5, r6, lsr #28
	mov	r6, r6, lsl #4
	mov	yh, yh, lsl #3
	orr	yh, yh, yl, lsr #29
	mov	yl, yl, lsl #3
	movs	ip, ip, lsr #4
	bne	1b

	@ We are done with a word of the result.
	@ Loop again for the low word if this pass was for the high word.
	tst	xh, #0x00100000
	bne	3f
	orr	xh, xh, xl
	mov	xl, #0
	mov	ip, #0x80000000
	b	1b
2:
	@ Be sure result starts in the high word.
	tst	xh, #0x00100000
	orreq	xh, xh, xl
	moveq	xl, #0
3:
	@ Check exponent range for under/overflow.
	subs	ip, r4, #(254 - 1)
	cmphi	ip, #0x700
	bhi	LSYM(Lml_u)

	@ Round the result, merge final exponent.
	subs	ip, r5, yh
	subeqs	ip, r6, yl
	moveqs	ip, xl, lsr #1
	adcs	xl, xl, #0
	adc	xh, xh, r4, lsl #20
	RETLDM	"r4, r5, r6"

	@ Division by 0x1p*: shortcut a lot of code.
LSYM(Ldv_1):
	and	lr, lr, #0x80000000
	orr	xh, lr, xh, lsr #12
	adds	r4, r4, ip, lsr #1
	rsbgts	r5, r4, ip
	orrgt	xh, xh, r4, lsl #20
	RETLDM	"r4, r5, r6" gt

	orr	xh, xh, #0x00100000
	mov	lr, #0
	subs	r4, r4, #1
	b	LSYM(Lml_u)

	@ Result mightt need to be denormalized: put remainder bits
	@ in lr for rounding considerations.
LSYM(Ldv_u):
	orr	lr, r5, r6
	b	LSYM(Lml_u)

	@ One or both arguments is either INF, NAN or zero.
LSYM(Ldv_s):
	and	r5, ip, yh, lsr #20
	teq	r4, ip
	teqeq	r5, ip
	beq	LSYM(Lml_n)		@ INF/NAN / INF/NAN -> NAN
	teq	r4, ip
	bne	1f
	orrs	r4, xl, xh, lsl #12
	bne	LSYM(Lml_n)		@ NAN / <anything> -> NAN
	teq	r5, ip
	bne	LSYM(Lml_i)		@ INF / <anything> -> INF
	mov	xl, yl
	mov	xh, yh
	b	LSYM(Lml_n)		@ INF / (INF or NAN) -> NAN
1:	teq	r5, ip
	bne	2f
	orrs	r5, yl, yh, lsl #12
	beq	LSYM(Lml_z)		@ <anything> / INF -> 0
	mov	xl, yl
	mov	xh, yh
	b	LSYM(Lml_n)		@ <anything> / NAN -> NAN
2:	@ If both are nonzero, we need to normalize and resume above.
	orrs	r6, xl, xh, lsl #1
	orrnes	r6, yl, yh, lsl #1
	bne	LSYM(Lml_d)
	@ One or both arguments are 0.
	orrs	r4, xl, xh, lsl #1
	bne	LSYM(Lml_i)		@ <non_zero> / 0 -> INF
	orrs	r5, yl, yh, lsl #1
	bne	LSYM(Lml_z)		@ 0 / <non_zero> -> 0
	b	LSYM(Lml_n)		@ 0 / 0 -> NAN

	FUNC_END aeabi_ddiv
	FUNC_END divdf3

#endif /* L_muldivdf3 */

#ifdef L_cmpdf2

@ Note: only r0 (return value) and ip are clobbered here.

ARM_FUNC_START gtdf2
ARM_FUNC_ALIAS gedf2 gtdf2
	mov	ip, #-1
	b	1f

ARM_FUNC_START ltdf2
ARM_FUNC_ALIAS ledf2 ltdf2
	mov	ip, #1
	b	1f

ARM_FUNC_START cmpdf2
ARM_FUNC_ALIAS nedf2 cmpdf2
ARM_FUNC_ALIAS eqdf2 cmpdf2
	mov	ip, #1			@ how should we specify unordered here?

1:	str	ip, [sp, #-4]

	@ Trap any INF/NAN first.
	mov	ip, xh, lsl #1
	mvns	ip, ip, asr #21
	mov	ip, yh, lsl #1
	mvnnes	ip, ip, asr #21
	beq	3f

	@ Test for equality.
	@ Note that 0.0 is equal to -0.0.
2:	orrs	ip, xl, xh, lsl #1	@ if x == 0.0 or -0.0
	orreqs	ip, yl, yh, lsl #1	@ and y == 0.0 or -0.0
	teqne	xh, yh			@ or xh == yh
	teqeq	xl, yl			@ and xl == yl
	moveq	r0, #0			@ then equal.
	RETc(eq)

	@ Clear C flag
	cmn	r0, #0

	@ Compare sign, 
	teq	xh, yh

	@ Compare values if same sign
	cmppl	xh, yh
	cmpeq	xl, yl

	@ Result:
	movcs	r0, yh, asr #31
	mvncc	r0, yh, asr #31
	orr	r0, r0, #1
	RET

	@ Look for a NAN.
3:	mov	ip, xh, lsl #1
	mvns	ip, ip, asr #21
	bne	4f
	orrs	ip, xl, xh, lsl #12
	bne	5f			@ x is NAN
4:	mov	ip, yh, lsl #1
	mvns	ip, ip, asr #21
	bne	2b
	orrs	ip, yl, yh, lsl #12
	beq	2b			@ y is not NAN
5:	ldr	r0, [sp, #-4]		@ unordered return code
	RET

	FUNC_END gedf2
	FUNC_END gtdf2
	FUNC_END ledf2
	FUNC_END ltdf2
	FUNC_END nedf2
	FUNC_END eqdf2
	FUNC_END cmpdf2

ARM_FUNC_START aeabi_cdrcmple

	mov	ip, r0
	mov	r0, r2
	mov	r2, ip
	mov	ip, r1
	mov	r1, r3
	mov	r3, ip
	b	6f
	
ARM_FUNC_START aeabi_cdcmpeq
ARM_FUNC_ALIAS aeabi_cdcmple aeabi_cdcmpeq

	@ The status-returning routines are required to preserve all
	@ registers except ip, lr, and cpsr.
6:	stmfd	sp!, {r0, lr}
	ARM_CALL cmpdf2
	@ Set the Z flag correctly, and the C flag unconditionally.
	cmp	 r0, #0
	@ Clear the C flag if the return value was -1, indicating
	@ that the first operand was smaller than the second.
	cmnmi	 r0, #0
	RETLDM   "r0"

	FUNC_END aeabi_cdcmple
	FUNC_END aeabi_cdcmpeq
	FUNC_END aeabi_cdrcmple
	
ARM_FUNC_START	aeabi_dcmpeq

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cdcmple
	moveq	r0, #1	@ Equal to.
	movne	r0, #0	@ Less than, greater than, or unordered.
	RETLDM

	FUNC_END aeabi_dcmpeq

ARM_FUNC_START	aeabi_dcmplt

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cdcmple
	movcc	r0, #1	@ Less than.
	movcs	r0, #0	@ Equal to, greater than, or unordered.
	RETLDM

	FUNC_END aeabi_dcmplt

ARM_FUNC_START	aeabi_dcmple

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cdcmple
	movls	r0, #1  @ Less than or equal to.
	movhi	r0, #0	@ Greater than or unordered.
	RETLDM

	FUNC_END aeabi_dcmple

ARM_FUNC_START	aeabi_dcmpge

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cdrcmple
	movls	r0, #1	@ Operand 2 is less than or equal to operand 1.
	movhi	r0, #0	@ Operand 2 greater than operand 1, or unordered.
	RETLDM

	FUNC_END aeabi_dcmpge

ARM_FUNC_START	aeabi_dcmpgt

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cdrcmple
	movcc	r0, #1	@ Operand 2 is less than operand 1.
	movcs	r0, #0  @ Operand 2 is greater than or equal to operand 1,
			@ or they are unordered.
	RETLDM

	FUNC_END aeabi_dcmpgt

#endif /* L_cmpdf2 */

#ifdef L_unorddf2

ARM_FUNC_START unorddf2
ARM_FUNC_ALIAS aeabi_dcmpun unorddf2

	mov	ip, xh, lsl #1
	mvns	ip, ip, asr #21
	bne	1f
	orrs	ip, xl, xh, lsl #12
	bne	3f			@ x is NAN
1:	mov	ip, yh, lsl #1
	mvns	ip, ip, asr #21
	bne	2f
	orrs	ip, yl, yh, lsl #12
	bne	3f			@ y is NAN
2:	mov	r0, #0			@ arguments are ordered.
	RET

3:	mov	r0, #1			@ arguments are unordered.
	RET

	FUNC_END aeabi_dcmpun
	FUNC_END unorddf2

#endif /* L_unorddf2 */

#ifdef L_fixdfsi

ARM_FUNC_START fixdfsi
ARM_FUNC_ALIAS aeabi_d2iz fixdfsi

	@ check exponent range.
	mov	r2, xh, lsl #1
	adds	r2, r2, #(1 << 21)
	bcs	2f			@ value is INF or NAN
	bpl	1f			@ value is too small
	mov	r3, #(0xfffffc00 + 31)
	subs	r2, r3, r2, asr #21
	bls	3f			@ value is too large

	@ scale value
	mov	r3, xh, lsl #11
	orr	r3, r3, #0x80000000
	orr	r3, r3, xl, lsr #21
	tst	xh, #0x80000000		@ the sign bit
	mov	r0, r3, lsr r2
	rsbne	r0, r0, #0
	RET

1:	mov	r0, #0
	RET

2:	orrs	xl, xl, xh, lsl #12
	bne	4f			@ x is NAN.
3:	ands	r0, xh, #0x80000000	@ the sign bit
	moveq	r0, #0x7fffffff		@ maximum signed positive si
	RET

4:	mov	r0, #0			@ How should we convert NAN?
	RET

	FUNC_END aeabi_d2iz
	FUNC_END fixdfsi

#endif /* L_fixdfsi */

#ifdef L_fixunsdfsi

ARM_FUNC_START fixunsdfsi
ARM_FUNC_ALIAS aeabi_d2uiz fixunsdfsi

	@ check exponent range.
	movs	r2, xh, lsl #1
	bcs	1f			@ value is negative
	adds	r2, r2, #(1 << 21)
	bcs	2f			@ value is INF or NAN
	bpl	1f			@ value is too small
	mov	r3, #(0xfffffc00 + 31)
	subs	r2, r3, r2, asr #21
	bmi	3f			@ value is too large

	@ scale value
	mov	r3, xh, lsl #11
	orr	r3, r3, #0x80000000
	orr	r3, r3, xl, lsr #21
	mov	r0, r3, lsr r2
	RET

1:	mov	r0, #0
	RET

2:	orrs	xl, xl, xh, lsl #12
	bne	4f			@ value is NAN.
3:	mov	r0, #0xffffffff		@ maximum unsigned si
	RET

4:	mov	r0, #0			@ How should we convert NAN?
	RET

	FUNC_END aeabi_d2uiz
	FUNC_END fixunsdfsi

#endif /* L_fixunsdfsi */

#ifdef L_truncdfsf2

ARM_FUNC_START truncdfsf2
ARM_FUNC_ALIAS aeabi_d2f truncdfsf2

	@ check exponent range.
	mov	r2, xh, lsl #1
	subs	r3, r2, #((1023 - 127) << 21)
	subcss	ip, r3, #(1 << 21)
	rsbcss	ip, ip, #(254 << 21)
	bls	2f			@ value is out of range

1:	@ shift and round mantissa
	and	ip, xh, #0x80000000
	mov	r2, xl, lsl #3
	orr	xl, ip, xl, lsr #29
	cmp	r2, #0x80000000
	adc	r0, xl, r3, lsl #2
	biceq	r0, r0, #1
	RET

2:	@ either overflow or underflow
	tst	xh, #0x40000000
	bne	3f			@ overflow

	@ check if denormalized value is possible
	adds	r2, r3, #(23 << 21)
	andlt	r0, xh, #0x80000000	@ too small, return signed 0.
	RETc(lt)

	@ denormalize value so we can resume with the code above afterwards.
	orr	xh, xh, #0x00100000
	mov	r2, r2, lsr #21
	rsb	r2, r2, #24
	rsb	ip, r2, #32
	movs	r3, xl, lsl ip
	mov	xl, xl, lsr r2
	orrne	xl, xl, #1		@ fold r3 for rounding considerations. 
	mov	r3, xh, lsl #11
	mov	r3, r3, lsr #11
	orr	xl, xl, r3, lsl ip
	mov	r3, r3, lsr r2
	mov	r3, r3, lsl #1
	b	1b

3:	@ chech for NAN
	mvns	r3, r2, asr #21
	bne	5f			@ simple overflow
	orrs	r3, xl, xh, lsl #12
	movne	r0, #0x7f000000
	orrne	r0, r0, #0x00c00000
	RETc(ne)			@ return NAN

5:	@ return INF with sign
	and	r0, xh, #0x80000000
	orr	r0, r0, #0x7f000000
	orr	r0, r0, #0x00800000
	RET

	FUNC_END aeabi_d2f
	FUNC_END truncdfsf2

#endif /* L_truncdfsf2 */
