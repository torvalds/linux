/* ieee754-sf.S single-precision floating point support for ARM

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
 *
 * Only the default rounding mode is intended for best performances.
 * Exceptions aren't supported yet, but that can be added quite easily
 * if necessary without impacting performances.
 */

#ifdef L_negsf2
	
ARM_FUNC_START negsf2
ARM_FUNC_ALIAS aeabi_fneg negsf2

	eor	r0, r0, #0x80000000	@ flip sign bit
	RET

	FUNC_END aeabi_fneg
	FUNC_END negsf2

#endif

#ifdef L_addsubsf3

ARM_FUNC_START aeabi_frsub

	eor	r0, r0, #0x80000000	@ flip sign bit of first arg
	b	1f

ARM_FUNC_START subsf3
ARM_FUNC_ALIAS aeabi_fsub subsf3

	eor	r1, r1, #0x80000000	@ flip sign bit of second arg
#if defined(__INTERWORKING_STUBS__)
	b	1f			@ Skip Thumb-code prologue
#endif

ARM_FUNC_START addsf3
ARM_FUNC_ALIAS aeabi_fadd addsf3

1:	@ Look for zeroes, equal values, INF, or NAN.
	movs	r2, r0, lsl #1
	movnes	r3, r1, lsl #1
	teqne	r2, r3
	mvnnes	ip, r2, asr #24
	mvnnes	ip, r3, asr #24
	beq	LSYM(Lad_s)

	@ Compute exponent difference.  Make largest exponent in r2,
	@ corresponding arg in r0, and positive exponent difference in r3.
	mov	r2, r2, lsr #24
	rsbs	r3, r2, r3, lsr #24
	addgt	r2, r2, r3
	eorgt	r1, r0, r1
	eorgt	r0, r1, r0
	eorgt	r1, r0, r1
	rsblt	r3, r3, #0

	@ If exponent difference is too large, return largest argument
	@ already in r0.  We need up to 25 bit to handle proper rounding
	@ of 0x1p25 - 1.1.
	cmp	r3, #25
	RETc(hi)

	@ Convert mantissa to signed integer.
	tst	r0, #0x80000000
	orr	r0, r0, #0x00800000
	bic	r0, r0, #0xff000000
	rsbne	r0, r0, #0
	tst	r1, #0x80000000
	orr	r1, r1, #0x00800000
	bic	r1, r1, #0xff000000
	rsbne	r1, r1, #0

	@ If exponent == difference, one or both args were denormalized.
	@ Since this is not common case, rescale them off line.
	teq	r2, r3
	beq	LSYM(Lad_d)
LSYM(Lad_x):

	@ Compensate for the exponent overlapping the mantissa MSB added later
	sub	r2, r2, #1

	@ Shift and add second arg to first arg in r0.
	@ Keep leftover bits into r1.
	adds	r0, r0, r1, asr r3
	rsb	r3, r3, #32
	mov	r1, r1, lsl r3

	@ Keep absolute value in r0-r1, sign in r3 (the n bit was set above)
	and	r3, r0, #0x80000000
	bpl	LSYM(Lad_p)
	rsbs	r1, r1, #0
	rsc	r0, r0, #0

	@ Determine how to normalize the result.
LSYM(Lad_p):
	cmp	r0, #0x00800000
	bcc	LSYM(Lad_a)
	cmp	r0, #0x01000000
	bcc	LSYM(Lad_e)

	@ Result needs to be shifted right.
	movs	r0, r0, lsr #1
	mov	r1, r1, rrx
	add	r2, r2, #1

	@ Make sure we did not bust our exponent.
	cmp	r2, #254
	bhs	LSYM(Lad_o)

	@ Our result is now properly aligned into r0, remaining bits in r1.
	@ Pack final result together.
	@ Round with MSB of r1. If halfway between two numbers, round towards
	@ LSB of r0 = 0. 
LSYM(Lad_e):
	cmp	r1, #0x80000000
	adc	r0, r0, r2, lsl #23
	biceq	r0, r0, #1
	orr	r0, r0, r3
	RET

	@ Result must be shifted left and exponent adjusted.
LSYM(Lad_a):
	movs	r1, r1, lsl #1
	adc	r0, r0, r0
	tst	r0, #0x00800000
	sub	r2, r2, #1
	bne	LSYM(Lad_e)
	
	@ No rounding necessary since r1 will always be 0 at this point.
LSYM(Lad_l):

#if __ARM_ARCH__ < 5

	movs	ip, r0, lsr #12
	moveq	r0, r0, lsl #12
	subeq	r2, r2, #12
	tst	r0, #0x00ff0000
	moveq	r0, r0, lsl #8
	subeq	r2, r2, #8
	tst	r0, #0x00f00000
	moveq	r0, r0, lsl #4
	subeq	r2, r2, #4
	tst	r0, #0x00c00000
	moveq	r0, r0, lsl #2
	subeq	r2, r2, #2
	cmp	r0, #0x00800000
	movcc	r0, r0, lsl #1
	sbcs	r2, r2, #0

#else

	clz	ip, r0
	sub	ip, ip, #8
	subs	r2, r2, ip
	mov	r0, r0, lsl ip

#endif

	@ Final result with sign
	@ If exponent negative, denormalize result.
	addge	r0, r0, r2, lsl #23
	rsblt	r2, r2, #0
	orrge	r0, r0, r3
	orrlt	r0, r3, r0, lsr r2
	RET

	@ Fixup and adjust bit position for denormalized arguments.
	@ Note that r2 must not remain equal to 0.
LSYM(Lad_d):
	teq	r2, #0
	eor	r1, r1, #0x00800000
	eoreq	r0, r0, #0x00800000
	addeq	r2, r2, #1
	subne	r3, r3, #1
	b	LSYM(Lad_x)

LSYM(Lad_s):
	mov	r3, r1, lsl #1

	mvns	ip, r2, asr #24
	mvnnes	ip, r3, asr #24
	beq	LSYM(Lad_i)

	teq	r2, r3
	beq	1f

	@ Result is x + 0.0 = x or 0.0 + y = y.
	teq	r2, #0
	moveq	r0, r1
	RET

1:	teq	r0, r1

	@ Result is x - x = 0.
	movne	r0, #0
	RETc(ne)

	@ Result is x + x = 2x.
	tst	r2, #0xff000000
	bne	2f
	movs	r0, r0, lsl #1
	orrcs	r0, r0, #0x80000000
	RET
2:	adds	r2, r2, #(2 << 24)
	addcc	r0, r0, #(1 << 23)
	RETc(cc)
	and	r3, r0, #0x80000000

	@ Overflow: return INF.
LSYM(Lad_o):
	orr	r0, r3, #0x7f000000
	orr	r0, r0, #0x00800000
	RET

	@ At least one of r0/r1 is INF/NAN.
	@   if r0 != INF/NAN: return r1 (which is INF/NAN)
	@   if r1 != INF/NAN: return r0 (which is INF/NAN)
	@   if r0 or r1 is NAN: return NAN
	@   if opposite sign: return NAN
	@   otherwise return r0 (which is INF or -INF)
LSYM(Lad_i):
	mvns	r2, r2, asr #24
	movne	r0, r1
	mvneqs	r3, r3, asr #24
	movne	r1, r0
	movs	r2, r0, lsl #9
	moveqs	r3, r1, lsl #9
	teqeq	r0, r1
	orrne	r0, r0, #0x00400000	@ quiet NAN
	RET

	FUNC_END aeabi_frsub
	FUNC_END aeabi_fadd
	FUNC_END addsf3
	FUNC_END aeabi_fsub
	FUNC_END subsf3

ARM_FUNC_START floatunsisf
ARM_FUNC_ALIAS aeabi_ui2f floatunsisf
		
	mov	r3, #0
	b	1f

ARM_FUNC_START floatsisf
ARM_FUNC_ALIAS aeabi_i2f floatsisf
	
	ands	r3, r0, #0x80000000
	rsbmi	r0, r0, #0

1:	movs	ip, r0
	RETc(eq)

	@ Add initial exponent to sign
	orr	r3, r3, #((127 + 23) << 23)

	.ifnc	ah, r0
	mov	ah, r0
	.endif
	mov	al, #0
	b	2f

	FUNC_END aeabi_i2f
	FUNC_END floatsisf
	FUNC_END aeabi_ui2f
	FUNC_END floatunsisf

ARM_FUNC_START floatundisf
ARM_FUNC_ALIAS aeabi_ul2f floatundisf

	orrs	r2, r0, r1
#if !defined (__VFP_FP__) && !defined(__SOFTFP__)
	mvfeqs	f0, #0.0
#endif
	RETc(eq)

	mov	r3, #0
	b	1f

ARM_FUNC_START floatdisf
ARM_FUNC_ALIAS aeabi_l2f floatdisf

	orrs	r2, r0, r1
#if !defined (__VFP_FP__) && !defined(__SOFTFP__)
	mvfeqs	f0, #0.0
#endif
	RETc(eq)

	ands	r3, ah, #0x80000000	@ sign bit in r3
	bpl	1f
	rsbs	al, al, #0
	rsc	ah, ah, #0
1:
#if !defined (__VFP_FP__) && !defined(__SOFTFP__)
	@ For hard FPA code we want to return via the tail below so that
	@ we can return the result in f0 as well as in r0 for backwards
	@ compatibility.
	str	lr, [sp, #-8]!
	adr	lr, LSYM(f0_ret)
#endif

	movs	ip, ah
	moveq	ip, al
	moveq	ah, al
	moveq	al, #0

	@ Add initial exponent to sign
	orr	r3, r3, #((127 + 23 + 32) << 23)
	subeq	r3, r3, #(32 << 23)
2:	sub	r3, r3, #(1 << 23)

#if __ARM_ARCH__ < 5

	mov	r2, #23
	cmp	ip, #(1 << 16)
	movhs	ip, ip, lsr #16
	subhs	r2, r2, #16
	cmp	ip, #(1 << 8)
	movhs	ip, ip, lsr #8
	subhs	r2, r2, #8
	cmp	ip, #(1 << 4)
	movhs	ip, ip, lsr #4
	subhs	r2, r2, #4
	cmp	ip, #(1 << 2)
	subhs	r2, r2, #2
	sublo	r2, r2, ip, lsr #1
	subs	r2, r2, ip, lsr #3

#else

	clz	r2, ip
	subs	r2, r2, #8

#endif

	sub	r3, r3, r2, lsl #23
	blt	3f

	add	r3, r3, ah, lsl r2
	mov	ip, al, lsl r2
	rsb	r2, r2, #32
	cmp	ip, #0x80000000
	adc	r0, r3, al, lsr r2
	biceq	r0, r0, #1
	RET

3:	add	r2, r2, #32
	mov	ip, ah, lsl r2
	rsb	r2, r2, #32
	orrs	al, al, ip, lsl #1
	adc	r0, r3, ah, lsr r2
	biceq	r0, r0, ip, lsr #31
	RET

#if !defined (__VFP_FP__) && !defined(__SOFTFP__)

LSYM(f0_ret):
	str	r0, [sp, #-4]!
	ldfs	f0, [sp], #4
	RETLDM

#endif

	FUNC_END floatdisf
	FUNC_END aeabi_l2f
	FUNC_END floatundisf
	FUNC_END aeabi_ul2f

#endif /* L_addsubsf3 */

#ifdef L_muldivsf3

ARM_FUNC_START mulsf3
ARM_FUNC_ALIAS aeabi_fmul mulsf3

	@ Mask out exponents, trap any zero/denormal/INF/NAN.
	mov	ip, #0xff
	ands	r2, ip, r0, lsr #23
	andnes	r3, ip, r1, lsr #23
	teqne	r2, ip
	teqne	r3, ip
	beq	LSYM(Lml_s)
LSYM(Lml_x):

	@ Add exponents together
	add	r2, r2, r3

	@ Determine final sign.
	eor	ip, r0, r1

	@ Convert mantissa to unsigned integer.
	@ If power of two, branch to a separate path.
	@ Make up for final alignment.
	movs	r0, r0, lsl #9
	movnes	r1, r1, lsl #9
	beq	LSYM(Lml_1)
	mov	r3, #0x08000000
	orr	r0, r3, r0, lsr #5
	orr	r1, r3, r1, lsr #5

#if __ARM_ARCH__ < 4

	@ Put sign bit in r3, which will be restored into r0 later.
	and	r3, ip, #0x80000000

	@ Well, no way to make it shorter without the umull instruction.
	stmfd	sp!, {r3, r4, r5}
	mov	r4, r0, lsr #16
	mov	r5, r1, lsr #16
	bic	r0, r0, r4, lsl #16
	bic	r1, r1, r5, lsl #16
	mul	ip, r4, r5
	mul	r3, r0, r1
	mul	r0, r5, r0
	mla	r0, r4, r1, r0
	adds	r3, r3, r0, lsl #16
	adc	r1, ip, r0, lsr #16
	ldmfd	sp!, {r0, r4, r5}

#else

	@ The actual multiplication.
	umull	r3, r1, r0, r1

	@ Put final sign in r0.
	and	r0, ip, #0x80000000

#endif

	@ Adjust result upon the MSB position.
	cmp	r1, #(1 << 23)
	movcc	r1, r1, lsl #1
	orrcc	r1, r1, r3, lsr #31
	movcc	r3, r3, lsl #1

	@ Add sign to result.
	orr	r0, r0, r1

	@ Apply exponent bias, check for under/overflow.
	sbc	r2, r2, #127
	cmp	r2, #(254 - 1)
	bhi	LSYM(Lml_u)

	@ Round the result, merge final exponent.
	cmp	r3, #0x80000000
	adc	r0, r0, r2, lsl #23
	biceq	r0, r0, #1
	RET

	@ Multiplication by 0x1p*: let''s shortcut a lot of code.
LSYM(Lml_1):
	teq	r0, #0
	and	ip, ip, #0x80000000
	moveq	r1, r1, lsl #9
	orr	r0, ip, r0, lsr #9
	orr	r0, r0, r1, lsr #9
	subs	r2, r2, #127
	rsbgts	r3, r2, #255
	orrgt	r0, r0, r2, lsl #23
	RETc(gt)

	@ Under/overflow: fix things up for the code below.
	orr	r0, r0, #0x00800000
	mov	r3, #0
	subs	r2, r2, #1

LSYM(Lml_u):
	@ Overflow?
	bgt	LSYM(Lml_o)

	@ Check if denormalized result is possible, otherwise return signed 0.
	cmn	r2, #(24 + 1)
	bicle	r0, r0, #0x7fffffff
	RETc(le)

	@ Shift value right, round, etc.
	rsb	r2, r2, #0
	movs	r1, r0, lsl #1
	mov	r1, r1, lsr r2
	rsb	r2, r2, #32
	mov	ip, r0, lsl r2
	movs	r0, r1, rrx
	adc	r0, r0, #0
	orrs	r3, r3, ip, lsl #1
	biceq	r0, r0, ip, lsr #31
	RET

	@ One or both arguments are denormalized.
	@ Scale them leftwards and preserve sign bit.
LSYM(Lml_d):
	teq	r2, #0
	and	ip, r0, #0x80000000
1:	moveq	r0, r0, lsl #1
	tsteq	r0, #0x00800000
	subeq	r2, r2, #1
	beq	1b
	orr	r0, r0, ip
	teq	r3, #0
	and	ip, r1, #0x80000000
2:	moveq	r1, r1, lsl #1
	tsteq	r1, #0x00800000
	subeq	r3, r3, #1
	beq	2b
	orr	r1, r1, ip
	b	LSYM(Lml_x)

LSYM(Lml_s):
	@ Isolate the INF and NAN cases away
	and	r3, ip, r1, lsr #23
	teq	r2, ip
	teqne	r3, ip
	beq	1f

	@ Here, one or more arguments are either denormalized or zero.
	bics	ip, r0, #0x80000000
	bicnes	ip, r1, #0x80000000
	bne	LSYM(Lml_d)

	@ Result is 0, but determine sign anyway.
LSYM(Lml_z):
	eor	r0, r0, r1
	bic	r0, r0, #0x7fffffff
	RET

1:	@ One or both args are INF or NAN.
	teq	r0, #0x0
	teqne	r0, #0x80000000
	moveq	r0, r1
	teqne	r1, #0x0
	teqne	r1, #0x80000000
	beq	LSYM(Lml_n)		@ 0 * INF or INF * 0 -> NAN
	teq	r2, ip
	bne	1f
	movs	r2, r0, lsl #9
	bne	LSYM(Lml_n)		@ NAN * <anything> -> NAN
1:	teq	r3, ip
	bne	LSYM(Lml_i)
	movs	r3, r1, lsl #9
	movne	r0, r1
	bne	LSYM(Lml_n)		@ <anything> * NAN -> NAN

	@ Result is INF, but we need to determine its sign.
LSYM(Lml_i):
	eor	r0, r0, r1

	@ Overflow: return INF (sign already in r0).
LSYM(Lml_o):
	and	r0, r0, #0x80000000
	orr	r0, r0, #0x7f000000
	orr	r0, r0, #0x00800000
	RET

	@ Return a quiet NAN.
LSYM(Lml_n):
	orr	r0, r0, #0x7f000000
	orr	r0, r0, #0x00c00000
	RET

	FUNC_END aeabi_fmul
	FUNC_END mulsf3

ARM_FUNC_START divsf3
ARM_FUNC_ALIAS aeabi_fdiv divsf3

	@ Mask out exponents, trap any zero/denormal/INF/NAN.
	mov	ip, #0xff
	ands	r2, ip, r0, lsr #23
	andnes	r3, ip, r1, lsr #23
	teqne	r2, ip
	teqne	r3, ip
	beq	LSYM(Ldv_s)
LSYM(Ldv_x):

	@ Substract divisor exponent from dividend''s
	sub	r2, r2, r3

	@ Preserve final sign into ip.
	eor	ip, r0, r1

	@ Convert mantissa to unsigned integer.
	@ Dividend -> r3, divisor -> r1.
	movs	r1, r1, lsl #9
	mov	r0, r0, lsl #9
	beq	LSYM(Ldv_1)
	mov	r3, #0x10000000
	orr	r1, r3, r1, lsr #4
	orr	r3, r3, r0, lsr #4

	@ Initialize r0 (result) with final sign bit.
	and	r0, ip, #0x80000000

	@ Ensure result will land to known bit position.
	@ Apply exponent bias accordingly.
	cmp	r3, r1
	movcc	r3, r3, lsl #1
	adc	r2, r2, #(127 - 2)

	@ The actual division loop.
	mov	ip, #0x00800000
1:	cmp	r3, r1
	subcs	r3, r3, r1
	orrcs	r0, r0, ip
	cmp	r3, r1, lsr #1
	subcs	r3, r3, r1, lsr #1
	orrcs	r0, r0, ip, lsr #1
	cmp	r3, r1, lsr #2
	subcs	r3, r3, r1, lsr #2
	orrcs	r0, r0, ip, lsr #2
	cmp	r3, r1, lsr #3
	subcs	r3, r3, r1, lsr #3
	orrcs	r0, r0, ip, lsr #3
	movs	r3, r3, lsl #4
	movnes	ip, ip, lsr #4
	bne	1b

	@ Check exponent for under/overflow.
	cmp	r2, #(254 - 1)
	bhi	LSYM(Lml_u)

	@ Round the result, merge final exponent.
	cmp	r3, r1
	adc	r0, r0, r2, lsl #23
	biceq	r0, r0, #1
	RET

	@ Division by 0x1p*: let''s shortcut a lot of code.
LSYM(Ldv_1):
	and	ip, ip, #0x80000000
	orr	r0, ip, r0, lsr #9
	adds	r2, r2, #127
	rsbgts	r3, r2, #255
	orrgt	r0, r0, r2, lsl #23
	RETc(gt)

	orr	r0, r0, #0x00800000
	mov	r3, #0
	subs	r2, r2, #1
	b	LSYM(Lml_u)

	@ One or both arguments are denormalized.
	@ Scale them leftwards and preserve sign bit.
LSYM(Ldv_d):
	teq	r2, #0
	and	ip, r0, #0x80000000
1:	moveq	r0, r0, lsl #1
	tsteq	r0, #0x00800000
	subeq	r2, r2, #1
	beq	1b
	orr	r0, r0, ip
	teq	r3, #0
	and	ip, r1, #0x80000000
2:	moveq	r1, r1, lsl #1
	tsteq	r1, #0x00800000
	subeq	r3, r3, #1
	beq	2b
	orr	r1, r1, ip
	b	LSYM(Ldv_x)

	@ One or both arguments are either INF, NAN, zero or denormalized.
LSYM(Ldv_s):
	and	r3, ip, r1, lsr #23
	teq	r2, ip
	bne	1f
	movs	r2, r0, lsl #9
	bne	LSYM(Lml_n)		@ NAN / <anything> -> NAN
	teq	r3, ip
	bne	LSYM(Lml_i)		@ INF / <anything> -> INF
	mov	r0, r1
	b	LSYM(Lml_n)		@ INF / (INF or NAN) -> NAN
1:	teq	r3, ip
	bne	2f
	movs	r3, r1, lsl #9
	beq	LSYM(Lml_z)		@ <anything> / INF -> 0
	mov	r0, r1
	b	LSYM(Lml_n)		@ <anything> / NAN -> NAN
2:	@ If both are nonzero, we need to normalize and resume above.
	bics	ip, r0, #0x80000000
	bicnes	ip, r1, #0x80000000
	bne	LSYM(Ldv_d)
	@ One or both arguments are zero.
	bics	r2, r0, #0x80000000
	bne	LSYM(Lml_i)		@ <non_zero> / 0 -> INF
	bics	r3, r1, #0x80000000
	bne	LSYM(Lml_z)		@ 0 / <non_zero> -> 0
	b	LSYM(Lml_n)		@ 0 / 0 -> NAN

	FUNC_END aeabi_fdiv
	FUNC_END divsf3

#endif /* L_muldivsf3 */

#ifdef L_cmpsf2

	@ The return value in r0 is
	@
	@   0  if the operands are equal
	@   1  if the first operand is greater than the second, or
	@      the operands are unordered and the operation is
	@      CMP, LT, LE, NE, or EQ.
	@   -1 if the first operand is less than the second, or
	@      the operands are unordered and the operation is GT
	@      or GE.
	@
	@ The Z flag will be set iff the operands are equal.
	@
	@ The following registers are clobbered by this function:
	@   ip, r0, r1, r2, r3

ARM_FUNC_START gtsf2
ARM_FUNC_ALIAS gesf2 gtsf2
	mov	ip, #-1
	b	1f

ARM_FUNC_START ltsf2
ARM_FUNC_ALIAS lesf2 ltsf2
	mov	ip, #1
	b	1f

ARM_FUNC_START cmpsf2
ARM_FUNC_ALIAS nesf2 cmpsf2
ARM_FUNC_ALIAS eqsf2 cmpsf2
	mov	ip, #1			@ how should we specify unordered here?

1:	str	ip, [sp, #-4]

	@ Trap any INF/NAN first.
	mov	r2, r0, lsl #1
	mov	r3, r1, lsl #1
	mvns	ip, r2, asr #24
	mvnnes	ip, r3, asr #24
	beq	3f

	@ Compare values.
	@ Note that 0.0 is equal to -0.0.
2:	orrs	ip, r2, r3, lsr #1	@ test if both are 0, clear C flag
	teqne	r0, r1			@ if not 0 compare sign
	subpls	r0, r2, r3		@ if same sign compare values, set r0

	@ Result:
	movhi	r0, r1, asr #31
	mvnlo	r0, r1, asr #31
	orrne	r0, r0, #1
	RET

	@ Look for a NAN. 
3:	mvns	ip, r2, asr #24
	bne	4f
	movs	ip, r0, lsl #9
	bne	5f			@ r0 is NAN
4:	mvns	ip, r3, asr #24
	bne	2b
	movs	ip, r1, lsl #9
	beq	2b			@ r1 is not NAN
5:	ldr	r0, [sp, #-4]		@ return unordered code.
	RET

	FUNC_END gesf2
	FUNC_END gtsf2
	FUNC_END lesf2
	FUNC_END ltsf2
	FUNC_END nesf2
	FUNC_END eqsf2
	FUNC_END cmpsf2

ARM_FUNC_START aeabi_cfrcmple

	mov	ip, r0
	mov	r0, r1
	mov	r1, ip
	b	6f

ARM_FUNC_START aeabi_cfcmpeq
ARM_FUNC_ALIAS aeabi_cfcmple aeabi_cfcmpeq

	@ The status-returning routines are required to preserve all
	@ registers except ip, lr, and cpsr.
6:	stmfd	sp!, {r0, r1, r2, r3, lr}
	ARM_CALL cmpsf2
	@ Set the Z flag correctly, and the C flag unconditionally.
	cmp	 r0, #0
	@ Clear the C flag if the return value was -1, indicating
	@ that the first operand was smaller than the second.
	cmnmi	 r0, #0
	RETLDM  "r0, r1, r2, r3"

	FUNC_END aeabi_cfcmple
	FUNC_END aeabi_cfcmpeq
	FUNC_END aeabi_cfrcmple

ARM_FUNC_START	aeabi_fcmpeq

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cfcmple
	moveq	r0, #1	@ Equal to.
	movne	r0, #0	@ Less than, greater than, or unordered.
	RETLDM

	FUNC_END aeabi_fcmpeq

ARM_FUNC_START	aeabi_fcmplt

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cfcmple
	movcc	r0, #1	@ Less than.
	movcs	r0, #0	@ Equal to, greater than, or unordered.
	RETLDM

	FUNC_END aeabi_fcmplt

ARM_FUNC_START	aeabi_fcmple

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cfcmple
	movls	r0, #1  @ Less than or equal to.
	movhi	r0, #0	@ Greater than or unordered.
	RETLDM

	FUNC_END aeabi_fcmple

ARM_FUNC_START	aeabi_fcmpge

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cfrcmple
	movls	r0, #1	@ Operand 2 is less than or equal to operand 1.
	movhi	r0, #0	@ Operand 2 greater than operand 1, or unordered.
	RETLDM

	FUNC_END aeabi_fcmpge

ARM_FUNC_START	aeabi_fcmpgt

	str	lr, [sp, #-8]!
	ARM_CALL aeabi_cfrcmple
	movcc	r0, #1	@ Operand 2 is less than operand 1.
	movcs	r0, #0  @ Operand 2 is greater than or equal to operand 1,
			@ or they are unordered.
	RETLDM

	FUNC_END aeabi_fcmpgt

#endif /* L_cmpsf2 */

#ifdef L_unordsf2

ARM_FUNC_START unordsf2
ARM_FUNC_ALIAS aeabi_fcmpun unordsf2

	mov	r2, r0, lsl #1
	mov	r3, r1, lsl #1
	mvns	ip, r2, asr #24
	bne	1f
	movs	ip, r0, lsl #9
	bne	3f			@ r0 is NAN
1:	mvns	ip, r3, asr #24
	bne	2f
	movs	ip, r1, lsl #9
	bne	3f			@ r1 is NAN
2:	mov	r0, #0			@ arguments are ordered.
	RET
3:	mov	r0, #1			@ arguments are unordered.
	RET

	FUNC_END aeabi_fcmpun
	FUNC_END unordsf2

#endif /* L_unordsf2 */

#ifdef L_fixsfsi

ARM_FUNC_START fixsfsi
ARM_FUNC_ALIAS aeabi_f2iz fixsfsi

	@ check exponent range.
	mov	r2, r0, lsl #1
	cmp	r2, #(127 << 24)
	bcc	1f			@ value is too small
	mov	r3, #(127 + 31)
	subs	r2, r3, r2, lsr #24
	bls	2f			@ value is too large

	@ scale value
	mov	r3, r0, lsl #8
	orr	r3, r3, #0x80000000
	tst	r0, #0x80000000		@ the sign bit
	mov	r0, r3, lsr r2
	rsbne	r0, r0, #0
	RET

1:	mov	r0, #0
	RET

2:	cmp	r2, #(127 + 31 - 0xff)
	bne	3f
	movs	r2, r0, lsl #9
	bne	4f			@ r0 is NAN.
3:	ands	r0, r0, #0x80000000	@ the sign bit
	moveq	r0, #0x7fffffff		@ the maximum signed positive si
	RET

4:	mov	r0, #0			@ What should we convert NAN to?
	RET

	FUNC_END aeabi_f2iz
	FUNC_END fixsfsi

#endif /* L_fixsfsi */

#ifdef L_fixunssfsi

ARM_FUNC_START fixunssfsi
ARM_FUNC_ALIAS aeabi_f2uiz fixunssfsi

	@ check exponent range.
	movs	r2, r0, lsl #1
	bcs	1f			@ value is negative
	cmp	r2, #(127 << 24)
	bcc	1f			@ value is too small
	mov	r3, #(127 + 31)
	subs	r2, r3, r2, lsr #24
	bmi	2f			@ value is too large

	@ scale the value
	mov	r3, r0, lsl #8
	orr	r3, r3, #0x80000000
	mov	r0, r3, lsr r2
	RET

1:	mov	r0, #0
	RET

2:	cmp	r2, #(127 + 31 - 0xff)
	bne	3f
	movs	r2, r0, lsl #9
	bne	4f			@ r0 is NAN.
3:	mov	r0, #0xffffffff		@ maximum unsigned si
	RET

4:	mov	r0, #0			@ What should we convert NAN to?
	RET

	FUNC_END aeabi_f2uiz
	FUNC_END fixunssfsi

#endif /* L_fixunssfsi */
