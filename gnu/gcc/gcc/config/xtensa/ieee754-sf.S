/* IEEE-754 single-precision functions for Xtensa
   Copyright (C) 2006 Free Software Foundation, Inc.
   Contributed by Bob Wilson (bwilson@tensilica.com) at Tensilica.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   In addition to the permissions in the GNU General Public License,
   the Free Software Foundation gives you unlimited permission to link
   the compiled version of this file into combinations with other
   programs, and to distribute those combinations without any
   restriction coming from the use of this file.  (The General Public
   License restrictions do apply in other respects; for example, they
   cover modification of the file, and distribution when not linked
   into a combine executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifdef __XTENSA_EB__
#define xh a2
#define xl a3
#define yh a4
#define yl a5
#else
#define xh a3
#define xl a2
#define yh a5
#define yl a4
#endif

/*  Warning!  The branch displacements for some Xtensa branch instructions
    are quite small, and this code has been carefully laid out to keep
    branch targets in range.  If you change anything, be sure to check that
    the assembler is not relaxing anything to branch over a jump.  */

#ifdef L_negsf2

	.align	4
	.global	__negsf2
	.type	__negsf2, @function
__negsf2:
	leaf_entry sp, 16
	movi	a4, 0x80000000
	xor	a2, a2, a4
	leaf_return

#endif /* L_negsf2 */

#ifdef L_addsubsf3

	/* Addition */
__addsf3_aux:

	/* Handle NaNs and Infinities.  (This code is placed before the
	   start of the function just to keep it in range of the limited
	   branch displacements.)  */

.Ladd_xnan_or_inf:
	/* If y is neither Infinity nor NaN, return x.  */
	bnall	a3, a6, 1f
	/* If x is a NaN, return it.  Otherwise, return y.  */
	slli	a7, a2, 9
	beqz	a7, .Ladd_ynan_or_inf
1:	leaf_return

.Ladd_ynan_or_inf:
	/* Return y.  */
	mov	a2, a3
	leaf_return

.Ladd_opposite_signs:
	/* Operand signs differ.  Do a subtraction.  */
	slli	a7, a6, 8
	xor	a3, a3, a7
	j	.Lsub_same_sign

	.align	4
	.global	__addsf3
	.type	__addsf3, @function
__addsf3:
	leaf_entry sp, 16
	movi	a6, 0x7f800000

	/* Check if the two operands have the same sign.  */
	xor	a7, a2, a3
	bltz	a7, .Ladd_opposite_signs

.Ladd_same_sign:	
	/* Check if either exponent == 0x7f8 (i.e., NaN or Infinity).  */
	ball	a2, a6, .Ladd_xnan_or_inf
	ball	a3, a6, .Ladd_ynan_or_inf

	/* Compare the exponents.  The smaller operand will be shifted
	   right by the exponent difference and added to the larger
	   one.  */
	extui	a7, a2, 23, 9
	extui	a8, a3, 23, 9
	bltu	a7, a8, .Ladd_shiftx

.Ladd_shifty:
	/* Check if the smaller (or equal) exponent is zero.  */
	bnone	a3, a6, .Ladd_yexpzero

	/* Replace y sign/exponent with 0x008.  */
	or	a3, a3, a6
	slli	a3, a3, 8
	srli	a3, a3, 8

.Ladd_yexpdiff:
	/* Compute the exponent difference.  */
	sub	a10, a7, a8

	/* Exponent difference > 32 -- just return the bigger value.  */
	bgeui	a10, 32, 1f
	
	/* Shift y right by the exponent difference.  Any bits that are
	   shifted out of y are saved in a9 for rounding the result.  */
	ssr	a10
	movi	a9, 0
	src	a9, a3, a9
	srl	a3, a3

	/* Do the addition.  */
	add	a2, a2, a3

	/* Check if the add overflowed into the exponent.  */
	extui	a10, a2, 23, 9
	beq	a10, a7, .Ladd_round
	mov	a8, a7
	j	.Ladd_carry

.Ladd_yexpzero:
	/* y is a subnormal value.  Replace its sign/exponent with zero,
	   i.e., no implicit "1.0", and increment the apparent exponent
	   because subnormals behave as if they had the minimum (nonzero)
	   exponent.  Test for the case when both exponents are zero.  */
	slli	a3, a3, 9
	srli	a3, a3, 9
	bnone	a2, a6, .Ladd_bothexpzero
	addi	a8, a8, 1
	j	.Ladd_yexpdiff

.Ladd_bothexpzero:
	/* Both exponents are zero.  Handle this as a special case.  There
	   is no need to shift or round, and the normal code for handling
	   a carry into the exponent field will not work because it
	   assumes there is an implicit "1.0" that needs to be added.  */
	add	a2, a2, a3
1:	leaf_return

.Ladd_xexpzero:
	/* Same as "yexpzero" except skip handling the case when both
	   exponents are zero.  */
	slli	a2, a2, 9
	srli	a2, a2, 9
	addi	a7, a7, 1
	j	.Ladd_xexpdiff

.Ladd_shiftx:
	/* Same thing as the "shifty" code, but with x and y swapped.  Also,
	   because the exponent difference is always nonzero in this version,
	   the shift sequence can use SLL and skip loading a constant zero.  */
	bnone	a2, a6, .Ladd_xexpzero

	or	a2, a2, a6
	slli	a2, a2, 8
	srli	a2, a2, 8

.Ladd_xexpdiff:
	sub	a10, a8, a7
	bgeui	a10, 32, .Ladd_returny
	
	ssr	a10
	sll	a9, a2
	srl	a2, a2

	add	a2, a2, a3

	/* Check if the add overflowed into the exponent.  */
	extui	a10, a2, 23, 9
	bne	a10, a8, .Ladd_carry

.Ladd_round:
	/* Round up if the leftover fraction is >= 1/2.  */
	bgez	a9, 1f
	addi	a2, a2, 1

	/* Check if the leftover fraction is exactly 1/2.  */
	slli	a9, a9, 1
	beqz	a9, .Ladd_exactlyhalf
1:	leaf_return

.Ladd_returny:
	mov	a2, a3
	leaf_return

.Ladd_carry:	
	/* The addition has overflowed into the exponent field, so the
	   value needs to be renormalized.  The mantissa of the result
	   can be recovered by subtracting the original exponent and
	   adding 0x800000 (which is the explicit "1.0" for the
	   mantissa of the non-shifted operand -- the "1.0" for the
	   shifted operand was already added).  The mantissa can then
	   be shifted right by one bit.  The explicit "1.0" of the
	   shifted mantissa then needs to be replaced by the exponent,
	   incremented by one to account for the normalizing shift.
	   It is faster to combine these operations: do the shift first
	   and combine the additions and subtractions.  If x is the
	   original exponent, the result is:
	       shifted mantissa - (x << 22) + (1 << 22) + (x << 23)
	   or:
	       shifted mantissa + ((x + 1) << 22)
	   Note that the exponent is incremented here by leaving the
	   explicit "1.0" of the mantissa in the exponent field.  */

	/* Shift x right by one bit.  Save the lsb.  */
	mov	a10, a2
	srli	a2, a2, 1

	/* See explanation above.  The original exponent is in a8.  */
	addi	a8, a8, 1
	slli	a8, a8, 22
	add	a2, a2, a8

	/* Return an Infinity if the exponent overflowed.  */
	ball	a2, a6, .Ladd_infinity
	
	/* Same thing as the "round" code except the msb of the leftover
	   fraction is bit 0 of a10, with the rest of the fraction in a9.  */
	bbci.l	a10, 0, 1f
	addi	a2, a2, 1
	beqz	a9, .Ladd_exactlyhalf
1:	leaf_return

.Ladd_infinity:
	/* Clear the mantissa.  */
	srli	a2, a2, 23
	slli	a2, a2, 23

	/* The sign bit may have been lost in a carry-out.  Put it back.  */
	slli	a8, a8, 1
	or	a2, a2, a8
	leaf_return

.Ladd_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	a2, a2, 1
	slli	a2, a2, 1
	leaf_return


	/* Subtraction */
__subsf3_aux:
	
	/* Handle NaNs and Infinities.  (This code is placed before the
	   start of the function just to keep it in range of the limited
	   branch displacements.)  */

.Lsub_xnan_or_inf:
	/* If y is neither Infinity nor NaN, return x.  */
	bnall	a3, a6, 1f
	/* Both x and y are either NaN or Inf, so the result is NaN.  */
	movi	a4, 0x400000	/* make it a quiet NaN */
	or	a2, a2, a4
1:	leaf_return

.Lsub_ynan_or_inf:
	/* Negate y and return it.  */
	slli	a7, a6, 8
	xor	a2, a3, a7
	leaf_return

.Lsub_opposite_signs:
	/* Operand signs differ.  Do an addition.  */
	slli	a7, a6, 8
	xor	a3, a3, a7
	j	.Ladd_same_sign

	.align	4
	.global	__subsf3
	.type	__subsf3, @function
__subsf3:
	leaf_entry sp, 16
	movi	a6, 0x7f800000

	/* Check if the two operands have the same sign.  */
	xor	a7, a2, a3
	bltz	a7, .Lsub_opposite_signs

.Lsub_same_sign:	
	/* Check if either exponent == 0x7f8 (i.e., NaN or Infinity).  */
	ball	a2, a6, .Lsub_xnan_or_inf
	ball	a3, a6, .Lsub_ynan_or_inf

	/* Compare the operands.  In contrast to addition, the entire
	   value matters here.  */
	extui	a7, a2, 23, 8
	extui	a8, a3, 23, 8
	bltu	a2, a3, .Lsub_xsmaller

.Lsub_ysmaller:
	/* Check if the smaller (or equal) exponent is zero.  */
	bnone	a3, a6, .Lsub_yexpzero

	/* Replace y sign/exponent with 0x008.  */
	or	a3, a3, a6
	slli	a3, a3, 8
	srli	a3, a3, 8

.Lsub_yexpdiff:
	/* Compute the exponent difference.  */
	sub	a10, a7, a8

	/* Exponent difference > 32 -- just return the bigger value.  */
	bgeui	a10, 32, 1f
	
	/* Shift y right by the exponent difference.  Any bits that are
	   shifted out of y are saved in a9 for rounding the result.  */
	ssr	a10
	movi	a9, 0
	src	a9, a3, a9
	srl	a3, a3

	sub	a2, a2, a3

	/* Subtract the leftover bits in a9 from zero and propagate any
	   borrow from a2.  */
	neg	a9, a9
	addi	a10, a2, -1
	movnez	a2, a10, a9

	/* Check if the subtract underflowed into the exponent.  */
	extui	a10, a2, 23, 8
	beq	a10, a7, .Lsub_round
	j	.Lsub_borrow

.Lsub_yexpzero:
	/* Return zero if the inputs are equal.  (For the non-subnormal
	   case, subtracting the "1.0" will cause a borrow from the exponent
	   and this case can be detected when handling the borrow.)  */
	beq	a2, a3, .Lsub_return_zero

	/* y is a subnormal value.  Replace its sign/exponent with zero,
	   i.e., no implicit "1.0".  Unless x is also a subnormal, increment
	   y's apparent exponent because subnormals behave as if they had
	   the minimum (nonzero) exponent.  */
	slli	a3, a3, 9
	srli	a3, a3, 9
	bnone	a2, a6, .Lsub_yexpdiff
	addi	a8, a8, 1
	j	.Lsub_yexpdiff

.Lsub_returny:
	/* Negate and return y.  */
	slli	a7, a6, 8
	xor	a2, a3, a7
1:	leaf_return

.Lsub_xsmaller:
	/* Same thing as the "ysmaller" code, but with x and y swapped and
	   with y negated.  */
	bnone	a2, a6, .Lsub_xexpzero

	or	a2, a2, a6
	slli	a2, a2, 8
	srli	a2, a2, 8

.Lsub_xexpdiff:
	sub	a10, a8, a7
	bgeui	a10, 32, .Lsub_returny
	
	ssr	a10
	movi	a9, 0
	src	a9, a2, a9
	srl	a2, a2

	/* Negate y.  */
	slli	a11, a6, 8
	xor	a3, a3, a11

	sub	a2, a3, a2

	neg	a9, a9
	addi	a10, a2, -1
	movnez	a2, a10, a9

	/* Check if the subtract underflowed into the exponent.  */
	extui	a10, a2, 23, 8
	bne	a10, a8, .Lsub_borrow

.Lsub_round:
	/* Round up if the leftover fraction is >= 1/2.  */
	bgez	a9, 1f
	addi	a2, a2, 1

	/* Check if the leftover fraction is exactly 1/2.  */
	slli	a9, a9, 1
	beqz	a9, .Lsub_exactlyhalf
1:	leaf_return

.Lsub_xexpzero:
	/* Same as "yexpzero".  */
	beq	a2, a3, .Lsub_return_zero
	slli	a2, a2, 9
	srli	a2, a2, 9
	bnone	a3, a6, .Lsub_xexpdiff
	addi	a7, a7, 1
	j	.Lsub_xexpdiff

.Lsub_return_zero:
	movi	a2, 0
	leaf_return

.Lsub_borrow:	
	/* The subtraction has underflowed into the exponent field, so the
	   value needs to be renormalized.  Shift the mantissa left as
	   needed to remove any leading zeros and adjust the exponent
	   accordingly.  If the exponent is not large enough to remove
	   all the leading zeros, the result will be a subnormal value.  */

	slli	a8, a2, 9
	beqz	a8, .Lsub_xzero
	do_nsau	a6, a8, a7, a11
	srli	a8, a8, 9
	bge	a6, a10, .Lsub_subnormal
	addi	a6, a6, 1

.Lsub_normalize_shift:
	/* Shift the mantissa (a8/a9) left by a6.  */
	ssl	a6
	src	a8, a8, a9
	sll	a9, a9

	/* Combine the shifted mantissa with the sign and exponent,
	   decrementing the exponent by a6.  (The exponent has already
	   been decremented by one due to the borrow from the subtraction,
	   but adding the mantissa will increment the exponent by one.)  */
	srli	a2, a2, 23
	sub	a2, a2, a6
	slli	a2, a2, 23
	add	a2, a2, a8
	j	.Lsub_round

.Lsub_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	a2, a2, 1
	slli	a2, a2, 1
	leaf_return

.Lsub_xzero:
	/* If there was a borrow from the exponent, and the mantissa and
	   guard digits are all zero, then the inputs were equal and the
	   result should be zero.  */
	beqz	a9, .Lsub_return_zero

	/* Only the guard digit is nonzero.  Shift by min(24, a10).  */
	addi	a11, a10, -24
	movi	a6, 24
	movltz	a6, a10, a11
	j	.Lsub_normalize_shift

.Lsub_subnormal:
	/* The exponent is too small to shift away all the leading zeros.
	   Set a6 to the current exponent (which has already been
	   decremented by the borrow) so that the exponent of the result
	   will be zero.  Do not add 1 to a6 in this case, because: (1)
	   adding the mantissa will not increment the exponent, so there is
	   no need to subtract anything extra from the exponent to
	   compensate, and (2) the effective exponent of a subnormal is 1
	   not 0 so the shift amount must be 1 smaller than normal. */
	mov	a6, a10
	j	.Lsub_normalize_shift

#endif /* L_addsubsf3 */

#ifdef L_mulsf3

	/* Multiplication */
__mulsf3_aux:

	/* Handle unusual cases (zeros, subnormals, NaNs and Infinities).
	   (This code is placed before the start of the function just to
	   keep it in range of the limited branch displacements.)  */

.Lmul_xexpzero:
	/* Clear the sign bit of x.  */
	slli	a2, a2, 1
	srli	a2, a2, 1

	/* If x is zero, return zero.  */
	beqz	a2, .Lmul_return_zero

	/* Normalize x.  Adjust the exponent in a8.  */
	do_nsau	a10, a2, a11, a12
	addi	a10, a10, -8
	ssl	a10
	sll	a2, a2 
	movi	a8, 1
	sub	a8, a8, a10
	j	.Lmul_xnormalized	
	
.Lmul_yexpzero:
	/* Clear the sign bit of y.  */
	slli	a3, a3, 1
	srli	a3, a3, 1

	/* If y is zero, return zero.  */
	beqz	a3, .Lmul_return_zero

	/* Normalize y.  Adjust the exponent in a9.  */
	do_nsau	a10, a3, a11, a12
	addi	a10, a10, -8
	ssl	a10
	sll	a3, a3
	movi	a9, 1
	sub	a9, a9, a10
	j	.Lmul_ynormalized	

.Lmul_return_zero:
	/* Return zero with the appropriate sign bit.  */
	srli	a2, a7, 31
	slli	a2, a2, 31
	j	.Lmul_done

.Lmul_xnan_or_inf:
	/* If y is zero, return NaN.  */
	slli	a8, a3, 1
	bnez	a8, 1f
	movi	a4, 0x400000	/* make it a quiet NaN */
	or	a2, a2, a4
	j	.Lmul_done
1:
	/* If y is NaN, return y.  */
	bnall	a3, a6, .Lmul_returnx
	slli	a8, a3, 9
	beqz	a8, .Lmul_returnx

.Lmul_returny:
	mov	a2, a3

.Lmul_returnx:
	/* Set the sign bit and return.  */
	extui	a7, a7, 31, 1
	slli	a2, a2, 1
	ssai	1
	src	a2, a7, a2
	j	.Lmul_done

.Lmul_ynan_or_inf:
	/* If x is zero, return NaN.  */
	slli	a8, a2, 1
	bnez	a8, .Lmul_returny
	movi	a7, 0x400000	/* make it a quiet NaN */
	or	a2, a3, a7
	j	.Lmul_done

	.align	4
	.global	__mulsf3
	.type	__mulsf3, @function
__mulsf3:
	leaf_entry sp, 32
#if __XTENSA_CALL0_ABI__
	addi	sp, sp, -32
	s32i	a12, sp, 16
	s32i	a13, sp, 20
	s32i	a14, sp, 24
	s32i	a15, sp, 28
#endif
	movi	a6, 0x7f800000

	/* Get the sign of the result.  */
	xor	a7, a2, a3

	/* Check for NaN and infinity.  */
	ball	a2, a6, .Lmul_xnan_or_inf
	ball	a3, a6, .Lmul_ynan_or_inf

	/* Extract the exponents.  */
	extui	a8, a2, 23, 8
	extui	a9, a3, 23, 8

	beqz	a8, .Lmul_xexpzero
.Lmul_xnormalized:	
	beqz	a9, .Lmul_yexpzero
.Lmul_ynormalized:	

	/* Add the exponents.  */
	add	a8, a8, a9

	/* Replace sign/exponent fields with explicit "1.0".  */
	movi	a10, 0xffffff
	or	a2, a2, a6
	and	a2, a2, a10
	or	a3, a3, a6
	and	a3, a3, a10

	/* Multiply 32x32 to 64 bits.  The result ends up in a2/a6.  */

#if XCHAL_HAVE_MUL32_HIGH

	mull	a6, a2, a3
	muluh	a2, a2, a3

#else

	/* Break the inputs into 16-bit chunks and compute 4 32-bit partial
	   products.  These partial products are:

		0 xl * yl

		1 xl * yh
		2 xh * yl

		3 xh * yh

	   If using the Mul16 or Mul32 multiplier options, these input
	   chunks must be stored in separate registers.  For Mac16, the
	   UMUL.AA.* opcodes can specify that the inputs come from either
	   half of the registers, so there is no need to shift them out
	   ahead of time.  If there is no multiply hardware, the 16-bit
	   chunks can be extracted when setting up the arguments to the
	   separate multiply function.  */

#if !XCHAL_HAVE_MUL16 && !XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MAC16
	/* Calling a separate multiply function will clobber a0 and requires
	   use of a8 as a temporary, so save those values now.  (The function
	   uses a custom ABI so nothing else needs to be saved.)  */
	s32i	a0, sp, 0
	s32i	a8, sp, 4
#endif

#if XCHAL_HAVE_MUL16 || XCHAL_HAVE_MUL32

#define a2h a4
#define a3h a5

	/* Get the high halves of the inputs into registers.  */
	srli	a2h, a2, 16
	srli	a3h, a3, 16

#define a2l a2
#define a3l a3

#if XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MUL16
	/* Clear the high halves of the inputs.  This does not matter
	   for MUL16 because the high bits are ignored.  */
	extui	a2, a2, 0, 16
	extui	a3, a3, 0, 16
#endif
#endif /* MUL16 || MUL32 */


#if XCHAL_HAVE_MUL16

#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	mul16u	dst, xreg ## xhalf, yreg ## yhalf

#elif XCHAL_HAVE_MUL32

#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	mull	dst, xreg ## xhalf, yreg ## yhalf

#elif XCHAL_HAVE_MAC16

/* The preprocessor insists on inserting a space when concatenating after
   a period in the definition of do_mul below.  These macros are a workaround
   using underscores instead of periods when doing the concatenation.  */
#define umul_aa_ll umul.aa.ll
#define umul_aa_lh umul.aa.lh
#define umul_aa_hl umul.aa.hl
#define umul_aa_hh umul.aa.hh

#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	umul_aa_ ## xhalf ## yhalf	xreg, yreg; \
	rsr	dst, ACCLO

#else /* no multiply hardware */
	
#define set_arg_l(dst, src) \
	extui	dst, src, 0, 16
#define set_arg_h(dst, src) \
	srli	dst, src, 16

#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	set_arg_ ## xhalf (a13, xreg); \
	set_arg_ ## yhalf (a14, yreg); \
	call0	.Lmul_mulsi3; \
	mov	dst, a12
#endif

	/* Add pp1 and pp2 into a6 with carry-out in a9.  */
	do_mul(a6, a2, l, a3, h)	/* pp 1 */
	do_mul(a11, a2, h, a3, l)	/* pp 2 */
	movi	a9, 0
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a9, a9, 1
1:
	/* Shift the high half of a9/a6 into position in a9.  Note that
	   this value can be safely incremented without any carry-outs.  */
	ssai	16
	src	a9, a9, a6

	/* Compute the low word into a6.  */
	do_mul(a11, a2, l, a3, l)	/* pp 0 */
	sll	a6, a6
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a9, a9, 1
1:
	/* Compute the high word into a2.  */
	do_mul(a2, a2, h, a3, h)	/* pp 3 */
	add	a2, a2, a9
	
#if !XCHAL_HAVE_MUL16 && !XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MAC16
	/* Restore values saved on the stack during the multiplication.  */
	l32i	a0, sp, 0
	l32i	a8, sp, 4
#endif
#endif

	/* Shift left by 9 bits, unless there was a carry-out from the
	   multiply, in which case, shift by 8 bits and increment the
	   exponent.  */
	movi	a4, 9
	srli	a5, a2, 24 - 9
	beqz	a5, 1f
	addi	a4, a4, -1
	addi	a8, a8, 1
1:	ssl	a4
	src	a2, a2, a6
	sll	a6, a6

	/* Subtract the extra bias from the exponent sum (plus one to account
	   for the explicit "1.0" of the mantissa that will be added to the
	   exponent in the final result).  */
	movi	a4, 0x80
	sub	a8, a8, a4
	
	/* Check for over/underflow.  The value in a8 is one less than the
	   final exponent, so values in the range 0..fd are OK here.  */
	movi	a4, 0xfe
	bgeu	a8, a4, .Lmul_overflow
	
.Lmul_round:
	/* Round.  */
	bgez	a6, .Lmul_rounded
	addi	a2, a2, 1
	slli	a6, a6, 1
	beqz	a6, .Lmul_exactlyhalf

.Lmul_rounded:
	/* Add the exponent to the mantissa.  */
	slli	a8, a8, 23
	add	a2, a2, a8

.Lmul_addsign:
	/* Add the sign bit.  */
	srli	a7, a7, 31
	slli	a7, a7, 31
	or	a2, a2, a7

.Lmul_done:
#if __XTENSA_CALL0_ABI__
	l32i	a12, sp, 16
	l32i	a13, sp, 20
	l32i	a14, sp, 24
	l32i	a15, sp, 28
	addi	sp, sp, 32
#endif
	leaf_return

.Lmul_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	a2, a2, 1
	slli	a2, a2, 1
	j	.Lmul_rounded

.Lmul_overflow:
	bltz	a8, .Lmul_underflow
	/* Return +/- Infinity.  */
	movi	a8, 0xff
	slli	a2, a8, 23
	j	.Lmul_addsign

.Lmul_underflow:
	/* Create a subnormal value, where the exponent field contains zero,
	   but the effective exponent is 1.  The value of a8 is one less than
	   the actual exponent, so just negate it to get the shift amount.  */
	neg	a8, a8
	mov	a9, a6
	ssr	a8
	bgeui	a8, 32, .Lmul_flush_to_zero
	
	/* Shift a2 right.  Any bits that are shifted out of a2 are saved
	   in a6 (combined with the shifted-out bits currently in a6) for
	   rounding the result.  */
	sll	a6, a2
	srl	a2, a2

	/* Set the exponent to zero.  */
	movi	a8, 0

	/* Pack any nonzero bits shifted out into a6.  */
	beqz	a9, .Lmul_round
	movi	a9, 1
	or	a6, a6, a9
	j	.Lmul_round
	
.Lmul_flush_to_zero:
	/* Return zero with the appropriate sign bit.  */
	srli	a2, a7, 31
	slli	a2, a2, 31
	j	.Lmul_done

#if !XCHAL_HAVE_MUL16 && !XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MAC16
	
	/* For Xtensa processors with no multiply hardware, this simplified
	   version of _mulsi3 is used for multiplying 16-bit chunks of
	   the floating-point mantissas.  It uses a custom ABI:	the inputs
	   are passed in a13 and a14, the result is returned in a12, and
	   a8 and a15 are clobbered.  */
	.align	4
.Lmul_mulsi3:
	movi	a12, 0
.Lmul_mult_loop:
	add	a15, a14, a12
	extui	a8, a13, 0, 1
	movnez	a12, a15, a8

	do_addx2 a15, a14, a12, a15
	extui	a8, a13, 1, 1
	movnez	a12, a15, a8

	do_addx4 a15, a14, a12, a15
	extui	a8, a13, 2, 1
	movnez	a12, a15, a8

	do_addx8 a15, a14, a12, a15
	extui	a8, a13, 3, 1
	movnez	a12, a15, a8

	srli	a13, a13, 4
	slli	a14, a14, 4
	bnez	a13, .Lmul_mult_loop
	ret
#endif /* !MUL16 && !MUL32 && !MAC16 */
#endif /* L_mulsf3 */

#ifdef L_divsf3

	/* Division */
__divsf3_aux:

	/* Handle unusual cases (zeros, subnormals, NaNs and Infinities).
	   (This code is placed before the start of the function just to
	   keep it in range of the limited branch displacements.)  */

.Ldiv_yexpzero:
	/* Clear the sign bit of y.  */
	slli	a3, a3, 1
	srli	a3, a3, 1

	/* Check for division by zero.  */
	beqz	a3, .Ldiv_yzero

	/* Normalize y.  Adjust the exponent in a9.  */
	do_nsau	a10, a3, a4, a5
	addi	a10, a10, -8
	ssl	a10
	sll	a3, a3
	movi	a9, 1
	sub	a9, a9, a10
	j	.Ldiv_ynormalized	

.Ldiv_yzero:
	/* y is zero.  Return NaN if x is also zero; otherwise, infinity.  */
	slli	a4, a2, 1
	srli	a4, a4, 1
	srli	a2, a7, 31
	slli	a2, a2, 31
	or	a2, a2, a6
	bnez	a4, 1f
	movi	a4, 0x400000	/* make it a quiet NaN */
	or	a2, a2, a4
1:	leaf_return

.Ldiv_xexpzero:
	/* Clear the sign bit of x.  */
	slli	a2, a2, 1
	srli	a2, a2, 1

	/* If x is zero, return zero.  */
	beqz	a2, .Ldiv_return_zero

	/* Normalize x.  Adjust the exponent in a8.  */
	do_nsau	a10, a2, a4, a5
	addi	a10, a10, -8
	ssl	a10
	sll	a2, a2
	movi	a8, 1
	sub	a8, a8, a10
	j	.Ldiv_xnormalized	
	
.Ldiv_return_zero:
	/* Return zero with the appropriate sign bit.  */
	srli	a2, a7, 31
	slli	a2, a2, 31
	leaf_return

.Ldiv_xnan_or_inf:
	/* Set the sign bit of the result.  */
	srli	a7, a3, 31
	slli	a7, a7, 31
	xor	a2, a2, a7
	/* If y is NaN or Inf, return NaN.  */
	bnall	a3, a6, 1f
	movi	a4, 0x400000	/* make it a quiet NaN */
	or	a2, a2, a4
1:	leaf_return

.Ldiv_ynan_or_inf:
	/* If y is Infinity, return zero.  */
	slli	a8, a3, 9
	beqz	a8, .Ldiv_return_zero
	/* y is NaN; return it.  */
	mov	a2, a3
	leaf_return

	.align	4
	.global	__divsf3
	.type	__divsf3, @function
__divsf3:
	leaf_entry sp, 16
	movi	a6, 0x7f800000

	/* Get the sign of the result.  */
	xor	a7, a2, a3

	/* Check for NaN and infinity.  */
	ball	a2, a6, .Ldiv_xnan_or_inf
	ball	a3, a6, .Ldiv_ynan_or_inf

	/* Extract the exponents.  */
	extui	a8, a2, 23, 8
	extui	a9, a3, 23, 8

	beqz	a9, .Ldiv_yexpzero
.Ldiv_ynormalized:	
	beqz	a8, .Ldiv_xexpzero
.Ldiv_xnormalized:	

	/* Subtract the exponents.  */
	sub	a8, a8, a9

	/* Replace sign/exponent fields with explicit "1.0".  */
	movi	a10, 0xffffff
	or	a2, a2, a6
	and	a2, a2, a10
	or	a3, a3, a6
	and	a3, a3, a10

	/* The first digit of the mantissa division must be a one.
	   Shift x (and adjust the exponent) as needed to make this true.  */
	bltu	a3, a2, 1f
	slli	a2, a2, 1
	addi	a8, a8, -1
1:
	/* Do the first subtraction and shift.  */
	sub	a2, a2, a3
	slli	a2, a2, 1

	/* Put the quotient into a10.  */
	movi	a10, 1

	/* Divide one bit at a time for 23 bits.  */
	movi	a9, 23
#if XCHAL_HAVE_LOOPS
	loop	a9, .Ldiv_loopend
#endif
.Ldiv_loop:
	/* Shift the quotient << 1.  */
	slli	a10, a10, 1

	/* Is this digit a 0 or 1?  */
	bltu	a2, a3, 1f

	/* Output a 1 and subtract.  */
	addi	a10, a10, 1
	sub	a2, a2, a3

	/* Shift the dividend << 1.  */
1:	slli	a2, a2, 1

#if !XCHAL_HAVE_LOOPS
	addi	a9, a9, -1
	bnez	a9, .Ldiv_loop
#endif
.Ldiv_loopend:

	/* Add the exponent bias (less one to account for the explicit "1.0"
	   of the mantissa that will be added to the exponent in the final
	   result).  */
	addi	a8, a8, 0x7e
	
	/* Check for over/underflow.  The value in a8 is one less than the
	   final exponent, so values in the range 0..fd are OK here.  */
	movi	a4, 0xfe
	bgeu	a8, a4, .Ldiv_overflow
	
.Ldiv_round:
	/* Round.  The remainder (<< 1) is in a2.  */
	bltu	a2, a3, .Ldiv_rounded
	addi	a10, a10, 1
	beq	a2, a3, .Ldiv_exactlyhalf

.Ldiv_rounded:
	/* Add the exponent to the mantissa.  */
	slli	a8, a8, 23
	add	a2, a10, a8

.Ldiv_addsign:
	/* Add the sign bit.  */
	srli	a7, a7, 31
	slli	a7, a7, 31
	or	a2, a2, a7
	leaf_return

.Ldiv_overflow:
	bltz	a8, .Ldiv_underflow
	/* Return +/- Infinity.  */
	addi	a8, a4, 1	/* 0xff */
	slli	a2, a8, 23
	j	.Ldiv_addsign

.Ldiv_exactlyhalf:
	/* Remainder is exactly half the divisor.  Round even.  */
	srli	a10, a10, 1
	slli	a10, a10, 1
	j	.Ldiv_rounded

.Ldiv_underflow:
	/* Create a subnormal value, where the exponent field contains zero,
	   but the effective exponent is 1.  The value of a8 is one less than
	   the actual exponent, so just negate it to get the shift amount.  */
	neg	a8, a8
	ssr	a8
	bgeui	a8, 32, .Ldiv_flush_to_zero
	
	/* Shift a10 right.  Any bits that are shifted out of a10 are
	   saved in a6 for rounding the result.  */
	sll	a6, a10
	srl	a10, a10

	/* Set the exponent to zero.  */
	movi	a8, 0

	/* Pack any nonzero remainder (in a2) into a6.  */
	beqz	a2, 1f
	movi	a9, 1
	or	a6, a6, a9
	
	/* Round a10 based on the bits shifted out into a6.  */
1:	bgez	a6, .Ldiv_rounded
	addi	a10, a10, 1
	slli	a6, a6, 1
	bnez	a6, .Ldiv_rounded
	srli	a10, a10, 1
	slli	a10, a10, 1
	j	.Ldiv_rounded

.Ldiv_flush_to_zero:
	/* Return zero with the appropriate sign bit.  */
	srli	a2, a7, 31
	slli	a2, a2, 31
	leaf_return

#endif /* L_divsf3 */

#ifdef L_cmpsf2

	/* Equal and Not Equal */

	.align	4
	.global	__eqsf2
	.global	__nesf2
	.set	__nesf2, __eqsf2
	.type	__eqsf2, @function
__eqsf2:
	leaf_entry sp, 16
	bne	a2, a3, 4f

	/* The values are equal but NaN != NaN.  Check the exponent.  */
	movi	a6, 0x7f800000
	ball	a2, a6, 3f

	/* Equal.  */
	movi	a2, 0
	leaf_return

	/* Not equal.  */
2:	movi	a2, 1
	leaf_return

	/* Check if the mantissas are nonzero.  */
3:	slli	a7, a2, 9
	j	5f

	/* Check if x and y are zero with different signs.  */
4:	or	a7, a2, a3
	slli	a7, a7, 1

	/* Equal if a7 == 0, where a7 is either abs(x | y) or the mantissa
	   or x when exponent(x) = 0x7f8 and x == y.  */
5:	movi	a2, 0
	movi	a3, 1
	movnez	a2, a3, a7	
	leaf_return


	/* Greater Than */

	.align	4
	.global	__gtsf2
	.type	__gtsf2, @function
__gtsf2:
	leaf_entry sp, 16
	movi	a6, 0x7f800000
	ball	a2, a6, 2f
1:	bnall	a3, a6, .Lle_cmp

	/* Check if y is a NaN.  */
	slli	a7, a3, 9
	beqz	a7, .Lle_cmp
	movi	a2, 0
	leaf_return

	/* Check if x is a NaN.  */
2:	slli	a7, a2, 9
	beqz	a7, 1b
	movi	a2, 0
	leaf_return


	/* Less Than or Equal */

	.align	4
	.global	__lesf2
	.type	__lesf2, @function
__lesf2:
	leaf_entry sp, 16
	movi	a6, 0x7f800000
	ball	a2, a6, 2f
1:	bnall	a3, a6, .Lle_cmp

	/* Check if y is a NaN.  */
	slli	a7, a3, 9
	beqz	a7, .Lle_cmp
	movi	a2, 1
	leaf_return

	/* Check if x is a NaN.  */
2:	slli	a7, a2, 9
	beqz	a7, 1b
	movi	a2, 1
	leaf_return

.Lle_cmp:
	/* Check if x and y have different signs.  */
	xor	a7, a2, a3
	bltz	a7, .Lle_diff_signs

	/* Check if x is negative.  */
	bltz	a2, .Lle_xneg

	/* Check if x <= y.  */
	bltu	a3, a2, 5f
4:	movi	a2, 0
	leaf_return

.Lle_xneg:
	/* Check if y <= x.  */
	bgeu	a2, a3, 4b
5:	movi	a2, 1
	leaf_return

.Lle_diff_signs:
	bltz	a2, 4b

	/* Check if both x and y are zero.  */
	or	a7, a2, a3
	slli	a7, a7, 1
	movi	a2, 1
	movi	a3, 0
	moveqz	a2, a3, a7
	leaf_return


	/* Greater Than or Equal */

	.align	4
	.global	__gesf2
	.type	__gesf2, @function
__gesf2:
	leaf_entry sp, 16
	movi	a6, 0x7f800000
	ball	a2, a6, 2f
1:	bnall	a3, a6, .Llt_cmp

	/* Check if y is a NaN.  */
	slli	a7, a3, 9
	beqz	a7, .Llt_cmp
	movi	a2, -1
	leaf_return

	/* Check if x is a NaN.  */
2:	slli	a7, a2, 9
	beqz	a7, 1b
	movi	a2, -1
	leaf_return


	/* Less Than */

	.align	4
	.global	__ltsf2
	.type	__ltsf2, @function
__ltsf2:
	leaf_entry sp, 16
	movi	a6, 0x7f800000
	ball	a2, a6, 2f
1:	bnall	a3, a6, .Llt_cmp

	/* Check if y is a NaN.  */
	slli	a7, a3, 9
	beqz	a7, .Llt_cmp
	movi	a2, 0
	leaf_return

	/* Check if x is a NaN.  */
2:	slli	a7, a2, 9
	beqz	a7, 1b
	movi	a2, 0
	leaf_return

.Llt_cmp:
	/* Check if x and y have different signs.  */
	xor	a7, a2, a3
	bltz	a7, .Llt_diff_signs

	/* Check if x is negative.  */
	bltz	a2, .Llt_xneg

	/* Check if x < y.  */
	bgeu	a2, a3, 5f
4:	movi	a2, -1
	leaf_return

.Llt_xneg:
	/* Check if y < x.  */
	bltu	a3, a2, 4b
5:	movi	a2, 0
	leaf_return

.Llt_diff_signs:
	bgez	a2, 5b

	/* Check if both x and y are nonzero.  */
	or	a7, a2, a3
	slli	a7, a7, 1
	movi	a2, 0
	movi	a3, -1
	movnez	a2, a3, a7
	leaf_return


	/* Unordered */

	.align	4
	.global	__unordsf2
	.type	__unordsf2, @function
__unordsf2:
	leaf_entry sp, 16
	movi	a6, 0x7f800000
	ball	a2, a6, 3f
1:	ball	a3, a6, 4f
2:	movi	a2, 0
	leaf_return

3:	slli	a7, a2, 9
	beqz	a7, 1b
	movi	a2, 1
	leaf_return

4:	slli	a7, a3, 9
	beqz	a7, 2b
	movi	a2, 1
	leaf_return

#endif /* L_cmpsf2 */

#ifdef L_fixsfsi

	.align	4
	.global	__fixsfsi
	.type	__fixsfsi, @function
__fixsfsi:
	leaf_entry sp, 16

	/* Check for NaN and Infinity.  */
	movi	a6, 0x7f800000
	ball	a2, a6, .Lfixsfsi_nan_or_inf

	/* Extract the exponent and check if 0 < (exp - 0x7e) < 32.  */
	extui	a4, a2, 23, 8
	addi	a4, a4, -0x7e
	bgei	a4, 32, .Lfixsfsi_maxint
	blti	a4, 1, .Lfixsfsi_zero

	/* Add explicit "1.0" and shift << 8.  */
	or	a7, a2, a6
	slli	a5, a7, 8

	/* Shift back to the right, based on the exponent.  */
	ssl	a4		/* shift by 32 - a4 */
	srl	a5, a5

	/* Negate the result if sign != 0.  */
	neg	a2, a5
	movgez	a2, a5, a7
	leaf_return

.Lfixsfsi_nan_or_inf:
	/* Handle Infinity and NaN.  */
	slli	a4, a2, 9
	beqz	a4, .Lfixsfsi_maxint

	/* Translate NaN to +maxint.  */
	movi	a2, 0

.Lfixsfsi_maxint:
	slli	a4, a6, 8	/* 0x80000000 */
	addi	a5, a4, -1	/* 0x7fffffff */
	movgez	a4, a5, a2
	mov	a2, a4
	leaf_return

.Lfixsfsi_zero:
	movi	a2, 0
	leaf_return

#endif /* L_fixsfsi */

#ifdef L_fixsfdi

	.align	4
	.global	__fixsfdi
	.type	__fixsfdi, @function
__fixsfdi:
	leaf_entry sp, 16

	/* Check for NaN and Infinity.  */
	movi	a6, 0x7f800000
	ball	a2, a6, .Lfixsfdi_nan_or_inf

	/* Extract the exponent and check if 0 < (exp - 0x7e) < 64.  */
	extui	a4, a2, 23, 8
	addi	a4, a4, -0x7e
	bgei	a4, 64, .Lfixsfdi_maxint
	blti	a4, 1, .Lfixsfdi_zero

	/* Add explicit "1.0" and shift << 8.  */
	or	a7, a2, a6
	slli	xh, a7, 8

	/* Shift back to the right, based on the exponent.  */
	ssl	a4		/* shift by 64 - a4 */
	bgei	a4, 32, .Lfixsfdi_smallshift
	srl	xl, xh
	movi	xh, 0

.Lfixsfdi_shifted:	
	/* Negate the result if sign != 0.  */
	bgez	a7, 1f
	neg	xl, xl
	neg	xh, xh
	beqz	xl, 1f
	addi	xh, xh, -1
1:	leaf_return

.Lfixsfdi_smallshift:
	movi	xl, 0
	sll	xl, xh
	srl	xh, xh
	j	.Lfixsfdi_shifted

.Lfixsfdi_nan_or_inf:
	/* Handle Infinity and NaN.  */
	slli	a4, a2, 9
	beqz	a4, .Lfixsfdi_maxint

	/* Translate NaN to +maxint.  */
	movi	a2, 0

.Lfixsfdi_maxint:
	slli	a7, a6, 8	/* 0x80000000 */
	bgez	a2, 1f
	mov	xh, a7
	movi	xl, 0
	leaf_return

1:	addi	xh, a7, -1	/* 0x7fffffff */
	movi	xl, -1
	leaf_return

.Lfixsfdi_zero:
	movi	xh, 0
	movi	xl, 0
	leaf_return

#endif /* L_fixsfdi */

#ifdef L_fixunssfsi

	.align	4
	.global	__fixunssfsi
	.type	__fixunssfsi, @function
__fixunssfsi:
	leaf_entry sp, 16

	/* Check for NaN and Infinity.  */
	movi	a6, 0x7f800000
	ball	a2, a6, .Lfixunssfsi_nan_or_inf

	/* Extract the exponent and check if 0 <= (exp - 0x7f) < 32.  */
	extui	a4, a2, 23, 8
	addi	a4, a4, -0x7f
	bgei	a4, 32, .Lfixunssfsi_maxint
	bltz	a4, .Lfixunssfsi_zero

	/* Add explicit "1.0" and shift << 8.  */
	or	a7, a2, a6
	slli	a5, a7, 8

	/* Shift back to the right, based on the exponent.  */
	addi	a4, a4, 1
	beqi	a4, 32, .Lfixunssfsi_bigexp
	ssl	a4		/* shift by 32 - a4 */
	srl	a5, a5

	/* Negate the result if sign != 0.  */
	neg	a2, a5
	movgez	a2, a5, a7
	leaf_return

.Lfixunssfsi_nan_or_inf:
	/* Handle Infinity and NaN.  */
	slli	a4, a2, 9
	beqz	a4, .Lfixunssfsi_maxint

	/* Translate NaN to 0xffffffff.  */
	movi	a2, -1
	leaf_return

.Lfixunssfsi_maxint:
	slli	a4, a6, 8	/* 0x80000000 */
	movi	a5, -1		/* 0xffffffff */
	movgez	a4, a5, a2
	mov	a2, a4
	leaf_return

.Lfixunssfsi_zero:
	movi	a2, 0
	leaf_return

.Lfixunssfsi_bigexp:
	/* Handle unsigned maximum exponent case.  */
	bltz	a2, 1f
	mov	a2, a5		/* no shift needed */
	leaf_return

	/* Return 0x80000000 if negative.  */
1:	slli	a2, a6, 8
	leaf_return

#endif /* L_fixunssfsi */

#ifdef L_fixunssfdi

	.align	4
	.global	__fixunssfdi
	.type	__fixunssfdi, @function
__fixunssfdi:
	leaf_entry sp, 16

	/* Check for NaN and Infinity.  */
	movi	a6, 0x7f800000
	ball	a2, a6, .Lfixunssfdi_nan_or_inf

	/* Extract the exponent and check if 0 <= (exp - 0x7f) < 64.  */
	extui	a4, a2, 23, 8
	addi	a4, a4, -0x7f
	bgei	a4, 64, .Lfixunssfdi_maxint
	bltz	a4, .Lfixunssfdi_zero

	/* Add explicit "1.0" and shift << 8.  */
	or	a7, a2, a6
	slli	xh, a7, 8

	/* Shift back to the right, based on the exponent.  */
	addi	a4, a4, 1
	beqi	a4, 64, .Lfixunssfdi_bigexp
	ssl	a4		/* shift by 64 - a4 */
	bgei	a4, 32, .Lfixunssfdi_smallshift
	srl	xl, xh
	movi	xh, 0

.Lfixunssfdi_shifted:
	/* Negate the result if sign != 0.  */
	bgez	a7, 1f
	neg	xl, xl
	neg	xh, xh
	beqz	xl, 1f
	addi	xh, xh, -1
1:	leaf_return

.Lfixunssfdi_smallshift:
	movi	xl, 0
	src	xl, xh, xl
	srl	xh, xh
	j	.Lfixunssfdi_shifted

.Lfixunssfdi_nan_or_inf:
	/* Handle Infinity and NaN.  */
	slli	a4, a2, 9
	beqz	a4, .Lfixunssfdi_maxint

	/* Translate NaN to 0xffffffff.... */
1:	movi	xh, -1
	movi	xl, -1
	leaf_return

.Lfixunssfdi_maxint:
	bgez	a2, 1b
2:	slli	xh, a6, 8	/* 0x80000000 */
	movi	xl, 0
	leaf_return

.Lfixunssfdi_zero:
	movi	xh, 0
	movi	xl, 0
	leaf_return

.Lfixunssfdi_bigexp:
	/* Handle unsigned maximum exponent case.  */
	bltz	a7, 2b
	movi	xl, 0
	leaf_return		/* no shift needed */

#endif /* L_fixunssfdi */

#ifdef L_floatsisf

	.align	4
	.global	__floatunsisf
	.type	__floatunsisf, @function
__floatunsisf:
	leaf_entry sp, 16
	beqz	a2, .Lfloatsisf_return

	/* Set the sign to zero and jump to the floatsisf code.  */
	movi	a7, 0
	j	.Lfloatsisf_normalize

	.align	4
	.global	__floatsisf
	.type	__floatsisf, @function
__floatsisf:
	leaf_entry sp, 16

	/* Check for zero.  */
	beqz	a2, .Lfloatsisf_return

	/* Save the sign.  */
	extui	a7, a2, 31, 1

	/* Get the absolute value.  */
#if XCHAL_HAVE_ABS
	abs	a2, a2
#else
	neg	a4, a2
	movltz	a2, a4, a2
#endif

.Lfloatsisf_normalize:
	/* Normalize with the first 1 bit in the msb.  */
	do_nsau	a4, a2, a5, a6
	ssl	a4
	sll	a5, a2

	/* Shift the mantissa into position, with rounding bits in a6.  */
	srli	a2, a5, 8
	slli	a6, a5, (32 - 8)

	/* Set the exponent.  */
	movi	a5, 0x9d	/* 0x7e + 31 */
	sub	a5, a5, a4
	slli	a5, a5, 23
	add	a2, a2, a5

	/* Add the sign.  */
	slli	a7, a7, 31
	or	a2, a2, a7

	/* Round up if the leftover fraction is >= 1/2.  */
	bgez	a6, .Lfloatsisf_return
	addi	a2, a2, 1	/* Overflow to the exponent is OK.  */

	/* Check if the leftover fraction is exactly 1/2.  */
	slli	a6, a6, 1
	beqz	a6, .Lfloatsisf_exactlyhalf

.Lfloatsisf_return:
	leaf_return

.Lfloatsisf_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	a2, a2, 1
	slli	a2, a2, 1
	leaf_return

#endif /* L_floatsisf */

#ifdef L_floatdisf

	.align	4
	.global	__floatundisf
	.type	__floatundisf, @function
__floatundisf:
	leaf_entry sp, 16

	/* Check for zero.  */
	or	a4, xh, xl
	beqz	a4, 2f

	/* Set the sign to zero and jump to the floatdisf code.  */
	movi	a7, 0
	j	.Lfloatdisf_normalize

	.align	4
	.global	__floatdisf
	.type	__floatdisf, @function
__floatdisf:
	leaf_entry sp, 16

	/* Check for zero.  */
	or	a4, xh, xl
	beqz	a4, 2f

	/* Save the sign.  */
	extui	a7, xh, 31, 1

	/* Get the absolute value.  */
	bgez	xh, .Lfloatdisf_normalize
	neg	xl, xl
	neg	xh, xh
	beqz	xl, .Lfloatdisf_normalize
	addi	xh, xh, -1

.Lfloatdisf_normalize:
	/* Normalize with the first 1 bit in the msb of xh.  */
	beqz	xh, .Lfloatdisf_bigshift
	do_nsau	a4, xh, a5, a6
	ssl	a4
	src	xh, xh, xl
	sll	xl, xl

.Lfloatdisf_shifted:
	/* Shift the mantissa into position, with rounding bits in a6.  */
	ssai	8
	sll	a5, xl
	src	a6, xh, xl
	srl	xh, xh
	beqz	a5, 1f
	movi	a5, 1
	or	a6, a6, a5
1:
	/* Set the exponent.  */
	movi	a5, 0xbd	/* 0x7e + 63 */
	sub	a5, a5, a4
	slli	a5, a5, 23
	add	a2, xh, a5

	/* Add the sign.  */
	slli	a7, a7, 31
	or	a2, a2, a7

	/* Round up if the leftover fraction is >= 1/2.  */
	bgez	a6, 2f
	addi	a2, a2, 1	/* Overflow to the exponent is OK.  */

	/* Check if the leftover fraction is exactly 1/2.  */
	slli	a6, a6, 1
	beqz	a6, .Lfloatdisf_exactlyhalf
2:	leaf_return

.Lfloatdisf_bigshift:
	/* xh is zero.  Normalize with first 1 bit of xl in the msb of xh.  */
	do_nsau	a4, xl, a5, a6
	ssl	a4
	sll	xh, xl
	movi	xl, 0
	addi	a4, a4, 32
	j	.Lfloatdisf_shifted

.Lfloatdisf_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	a2, a2, 1
	slli	a2, a2, 1
	leaf_return

#endif /* L_floatdisf */
