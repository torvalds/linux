/* IEEE-754 double-precision functions for Xtensa
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

#ifdef L_negdf2

	.align	4
	.global	__negdf2
	.type	__negdf2, @function
__negdf2:
	leaf_entry sp, 16
	movi	a4, 0x80000000
	xor	xh, xh, a4
	leaf_return

#endif /* L_negdf2 */

#ifdef L_addsubdf3

	/* Addition */
__adddf3_aux:
	
	/* Handle NaNs and Infinities.  (This code is placed before the
	   start of the function just to keep it in range of the limited
	   branch displacements.)  */

.Ladd_xnan_or_inf:
	/* If y is neither Infinity nor NaN, return x.  */
	bnall	yh, a6, 1f
	/* If x is a NaN, return it.  Otherwise, return y.  */
	slli	a7, xh, 12
	or	a7, a7, xl
	beqz	a7, .Ladd_ynan_or_inf
1:	leaf_return

.Ladd_ynan_or_inf:
	/* Return y.  */
	mov	xh, yh
	mov	xl, yl
	leaf_return

.Ladd_opposite_signs:
	/* Operand signs differ.  Do a subtraction.  */
	slli	a7, a6, 11
	xor	yh, yh, a7
	j	.Lsub_same_sign

	.align	4
	.global	__adddf3
	.type	__adddf3, @function
__adddf3:
	leaf_entry sp, 16
	movi	a6, 0x7ff00000

	/* Check if the two operands have the same sign.  */
	xor	a7, xh, yh
	bltz	a7, .Ladd_opposite_signs

.Ladd_same_sign:	
	/* Check if either exponent == 0x7ff (i.e., NaN or Infinity).  */
	ball	xh, a6, .Ladd_xnan_or_inf
	ball	yh, a6, .Ladd_ynan_or_inf

	/* Compare the exponents.  The smaller operand will be shifted
	   right by the exponent difference and added to the larger
	   one.  */
	extui	a7, xh, 20, 12
	extui	a8, yh, 20, 12
	bltu	a7, a8, .Ladd_shiftx

.Ladd_shifty:
	/* Check if the smaller (or equal) exponent is zero.  */
	bnone	yh, a6, .Ladd_yexpzero

	/* Replace yh sign/exponent with 0x001.  */
	or	yh, yh, a6
	slli	yh, yh, 11
	srli	yh, yh, 11

.Ladd_yexpdiff:
	/* Compute the exponent difference.  Optimize for difference < 32.  */
	sub	a10, a7, a8
	bgeui	a10, 32, .Ladd_bigshifty
	
	/* Shift yh/yl right by the exponent difference.  Any bits that are
	   shifted out of yl are saved in a9 for rounding the result.  */
	ssr	a10
	movi	a9, 0
	src	a9, yl, a9
	src	yl, yh, yl
	srl	yh, yh

.Ladd_addy:
	/* Do the 64-bit addition.  */
	add	xl, xl, yl
	add	xh, xh, yh
	bgeu	xl, yl, 1f
	addi	xh, xh, 1
1:
	/* Check if the add overflowed into the exponent.  */
	extui	a10, xh, 20, 12
	beq	a10, a7, .Ladd_round
	mov	a8, a7
	j	.Ladd_carry

.Ladd_yexpzero:
	/* y is a subnormal value.  Replace its sign/exponent with zero,
	   i.e., no implicit "1.0", and increment the apparent exponent
	   because subnormals behave as if they had the minimum (nonzero)
	   exponent.  Test for the case when both exponents are zero.  */
	slli	yh, yh, 12
	srli	yh, yh, 12
	bnone	xh, a6, .Ladd_bothexpzero
	addi	a8, a8, 1
	j	.Ladd_yexpdiff

.Ladd_bothexpzero:
	/* Both exponents are zero.  Handle this as a special case.  There
	   is no need to shift or round, and the normal code for handling
	   a carry into the exponent field will not work because it
	   assumes there is an implicit "1.0" that needs to be added.  */
	add	xl, xl, yl
	add	xh, xh, yh
	bgeu	xl, yl, 1f
	addi	xh, xh, 1
1:	leaf_return

.Ladd_bigshifty:
	/* Exponent difference > 64 -- just return the bigger value.  */
	bgeui	a10, 64, 1b

	/* Shift yh/yl right by the exponent difference.  Any bits that are
	   shifted out are saved in a9 for rounding the result.  */
	ssr	a10
	sll	a11, yl		/* lost bits shifted out of yl */
	src	a9, yh, yl
	srl	yl, yh
	movi	yh, 0
	beqz	a11, .Ladd_addy
	or	a9, a9, a10	/* any positive, nonzero value will work */
	j	.Ladd_addy

.Ladd_xexpzero:
	/* Same as "yexpzero" except skip handling the case when both
	   exponents are zero.  */
	slli	xh, xh, 12
	srli	xh, xh, 12
	addi	a7, a7, 1
	j	.Ladd_xexpdiff

.Ladd_shiftx:
	/* Same thing as the "shifty" code, but with x and y swapped.  Also,
	   because the exponent difference is always nonzero in this version,
	   the shift sequence can use SLL and skip loading a constant zero.  */
	bnone	xh, a6, .Ladd_xexpzero

	or	xh, xh, a6
	slli	xh, xh, 11
	srli	xh, xh, 11

.Ladd_xexpdiff:
	sub	a10, a8, a7
	bgeui	a10, 32, .Ladd_bigshiftx
	
	ssr	a10
	sll	a9, xl
	src	xl, xh, xl
	srl	xh, xh

.Ladd_addx:
	add	xl, xl, yl
	add	xh, xh, yh
	bgeu	xl, yl, 1f
	addi	xh, xh, 1
1:
	/* Check if the add overflowed into the exponent.  */
	extui	a10, xh, 20, 12
	bne	a10, a8, .Ladd_carry

.Ladd_round:
	/* Round up if the leftover fraction is >= 1/2.  */
	bgez	a9, 1f
	addi	xl, xl, 1
	beqz	xl, .Ladd_roundcarry

	/* Check if the leftover fraction is exactly 1/2.  */
	slli	a9, a9, 1
	beqz	a9, .Ladd_exactlyhalf
1:	leaf_return

.Ladd_bigshiftx:
	/* Mostly the same thing as "bigshifty"....  */
	bgeui	a10, 64, .Ladd_returny

	ssr	a10
	sll	a11, xl
	src	a9, xh, xl
	srl	xl, xh
	movi	xh, 0
	beqz	a11, .Ladd_addx
	or	a9, a9, a10
	j	.Ladd_addx

.Ladd_returny:
	mov	xh, yh
	mov	xl, yl
	leaf_return

.Ladd_carry:	
	/* The addition has overflowed into the exponent field, so the
	   value needs to be renormalized.  The mantissa of the result
	   can be recovered by subtracting the original exponent and
	   adding 0x100000 (which is the explicit "1.0" for the
	   mantissa of the non-shifted operand -- the "1.0" for the
	   shifted operand was already added).  The mantissa can then
	   be shifted right by one bit.  The explicit "1.0" of the
	   shifted mantissa then needs to be replaced by the exponent,
	   incremented by one to account for the normalizing shift.
	   It is faster to combine these operations: do the shift first
	   and combine the additions and subtractions.  If x is the
	   original exponent, the result is:
	       shifted mantissa - (x << 19) + (1 << 19) + (x << 20)
	   or:
	       shifted mantissa + ((x + 1) << 19)
	   Note that the exponent is incremented here by leaving the
	   explicit "1.0" of the mantissa in the exponent field.  */

	/* Shift xh/xl right by one bit.  Save the lsb of xl.  */
	mov	a10, xl
	ssai	1
	src	xl, xh, xl
	srl	xh, xh

	/* See explanation above.  The original exponent is in a8.  */
	addi	a8, a8, 1
	slli	a8, a8, 19
	add	xh, xh, a8

	/* Return an Infinity if the exponent overflowed.  */
	ball	xh, a6, .Ladd_infinity
	
	/* Same thing as the "round" code except the msb of the leftover
	   fraction is bit 0 of a10, with the rest of the fraction in a9.  */
	bbci.l	a10, 0, 1f
	addi	xl, xl, 1
	beqz	xl, .Ladd_roundcarry
	beqz	a9, .Ladd_exactlyhalf
1:	leaf_return

.Ladd_infinity:
	/* Clear the mantissa.  */
	movi	xl, 0
	srli	xh, xh, 20
	slli	xh, xh, 20

	/* The sign bit may have been lost in a carry-out.  Put it back.  */
	slli	a8, a8, 1
	or	xh, xh, a8
	leaf_return

.Ladd_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	xl, xl, 1
	slli	xl, xl, 1
	leaf_return

.Ladd_roundcarry:
	/* xl is always zero when the rounding increment overflows, so
	   there's no need to round it to an even value.  */
	addi	xh, xh, 1
	/* Overflow to the exponent is OK.  */
	leaf_return


	/* Subtraction */
__subdf3_aux:
	
	/* Handle NaNs and Infinities.  (This code is placed before the
	   start of the function just to keep it in range of the limited
	   branch displacements.)  */

.Lsub_xnan_or_inf:
	/* If y is neither Infinity nor NaN, return x.  */
	bnall	yh, a6, 1f
	/* Both x and y are either NaN or Inf, so the result is NaN.  */
	movi	a4, 0x80000	/* make it a quiet NaN */
	or	xh, xh, a4
1:	leaf_return

.Lsub_ynan_or_inf:
	/* Negate y and return it.  */
	slli	a7, a6, 11
	xor	xh, yh, a7
	mov	xl, yl
	leaf_return

.Lsub_opposite_signs:
	/* Operand signs differ.  Do an addition.  */
	slli	a7, a6, 11
	xor	yh, yh, a7
	j	.Ladd_same_sign

	.align	4
	.global	__subdf3
	.type	__subdf3, @function
__subdf3:
	leaf_entry sp, 16
	movi	a6, 0x7ff00000

	/* Check if the two operands have the same sign.  */
	xor	a7, xh, yh
	bltz	a7, .Lsub_opposite_signs

.Lsub_same_sign:	
	/* Check if either exponent == 0x7ff (i.e., NaN or Infinity).  */
	ball	xh, a6, .Lsub_xnan_or_inf
	ball	yh, a6, .Lsub_ynan_or_inf

	/* Compare the operands.  In contrast to addition, the entire
	   value matters here.  */
	extui	a7, xh, 20, 11
	extui	a8, yh, 20, 11
	bltu	xh, yh, .Lsub_xsmaller
	beq	xh, yh, .Lsub_compare_low

.Lsub_ysmaller:
	/* Check if the smaller (or equal) exponent is zero.  */
	bnone	yh, a6, .Lsub_yexpzero

	/* Replace yh sign/exponent with 0x001.  */
	or	yh, yh, a6
	slli	yh, yh, 11
	srli	yh, yh, 11

.Lsub_yexpdiff:
	/* Compute the exponent difference.  Optimize for difference < 32.  */
	sub	a10, a7, a8
	bgeui	a10, 32, .Lsub_bigshifty
	
	/* Shift yh/yl right by the exponent difference.  Any bits that are
	   shifted out of yl are saved in a9 for rounding the result.  */
	ssr	a10
	movi	a9, 0
	src	a9, yl, a9
	src	yl, yh, yl
	srl	yh, yh

.Lsub_suby:
	/* Do the 64-bit subtraction.  */
	sub	xh, xh, yh
	bgeu	xl, yl, 1f
	addi	xh, xh, -1
1:	sub	xl, xl, yl

	/* Subtract the leftover bits in a9 from zero and propagate any
	   borrow from xh/xl.  */
	neg	a9, a9
	beqz	a9, 1f
	addi	a5, xh, -1
	moveqz	xh, a5, xl
	addi	xl, xl, -1
1:
	/* Check if the subtract underflowed into the exponent.  */
	extui	a10, xh, 20, 11
	beq	a10, a7, .Lsub_round
	j	.Lsub_borrow

.Lsub_compare_low:
	/* The high words are equal.  Compare the low words.  */
	bltu	xl, yl, .Lsub_xsmaller
	bltu	yl, xl, .Lsub_ysmaller
	/* The operands are equal.  Return 0.0.  */
	movi	xh, 0
	movi	xl, 0
1:	leaf_return

.Lsub_yexpzero:
	/* y is a subnormal value.  Replace its sign/exponent with zero,
	   i.e., no implicit "1.0".  Unless x is also a subnormal, increment
	   y's apparent exponent because subnormals behave as if they had
	   the minimum (nonzero) exponent.  */
	slli	yh, yh, 12
	srli	yh, yh, 12
	bnone	xh, a6, .Lsub_yexpdiff
	addi	a8, a8, 1
	j	.Lsub_yexpdiff

.Lsub_bigshifty:
	/* Exponent difference > 64 -- just return the bigger value.  */
	bgeui	a10, 64, 1b

	/* Shift yh/yl right by the exponent difference.  Any bits that are
	   shifted out are saved in a9 for rounding the result.  */
	ssr	a10
	sll	a11, yl		/* lost bits shifted out of yl */
	src	a9, yh, yl
	srl	yl, yh
	movi	yh, 0
	beqz	a11, .Lsub_suby
	or	a9, a9, a10	/* any positive, nonzero value will work */
	j	.Lsub_suby

.Lsub_xsmaller:
	/* Same thing as the "ysmaller" code, but with x and y swapped and
	   with y negated.  */
	bnone	xh, a6, .Lsub_xexpzero

	or	xh, xh, a6
	slli	xh, xh, 11
	srli	xh, xh, 11

.Lsub_xexpdiff:
	sub	a10, a8, a7
	bgeui	a10, 32, .Lsub_bigshiftx
	
	ssr	a10
	movi	a9, 0
	src	a9, xl, a9
	src	xl, xh, xl
	srl	xh, xh

	/* Negate y.  */
	slli	a11, a6, 11
	xor	yh, yh, a11

.Lsub_subx:
	sub	xl, yl, xl
	sub	xh, yh, xh
	bgeu	yl, xl, 1f
	addi	xh, xh, -1
1:
	/* Subtract the leftover bits in a9 from zero and propagate any
	   borrow from xh/xl.  */
	neg	a9, a9
	beqz	a9, 1f
	addi	a5, xh, -1
	moveqz	xh, a5, xl
	addi	xl, xl, -1
1:
	/* Check if the subtract underflowed into the exponent.  */
	extui	a10, xh, 20, 11
	bne	a10, a8, .Lsub_borrow

.Lsub_round:
	/* Round up if the leftover fraction is >= 1/2.  */
	bgez	a9, 1f
	addi	xl, xl, 1
	beqz	xl, .Lsub_roundcarry

	/* Check if the leftover fraction is exactly 1/2.  */
	slli	a9, a9, 1
	beqz	a9, .Lsub_exactlyhalf
1:	leaf_return

.Lsub_xexpzero:
	/* Same as "yexpzero".  */
	slli	xh, xh, 12
	srli	xh, xh, 12
	bnone	yh, a6, .Lsub_xexpdiff
	addi	a7, a7, 1
	j	.Lsub_xexpdiff

.Lsub_bigshiftx:
	/* Mostly the same thing as "bigshifty", but with the sign bit of the
	   shifted value set so that the subsequent subtraction flips the
	   sign of y.  */
	bgeui	a10, 64, .Lsub_returny

	ssr	a10
	sll	a11, xl
	src	a9, xh, xl
	srl	xl, xh
	slli	xh, a6, 11	/* set sign bit of xh */
	beqz	a11, .Lsub_subx
	or	a9, a9, a10
	j	.Lsub_subx

.Lsub_returny:
	/* Negate and return y.  */
	slli	a7, a6, 11
	xor	xh, yh, a7
	mov	xl, yl
	leaf_return

.Lsub_borrow:	
	/* The subtraction has underflowed into the exponent field, so the
	   value needs to be renormalized.  Shift the mantissa left as
	   needed to remove any leading zeros and adjust the exponent
	   accordingly.  If the exponent is not large enough to remove
	   all the leading zeros, the result will be a subnormal value.  */

	slli	a8, xh, 12
	beqz	a8, .Lsub_xhzero
	do_nsau	a6, a8, a7, a11
	srli	a8, a8, 12
	bge	a6, a10, .Lsub_subnormal
	addi	a6, a6, 1

.Lsub_shift_lt32:
	/* Shift the mantissa (a8/xl/a9) left by a6.  */
	ssl	a6
	src	a8, a8, xl
	src	xl, xl, a9
	sll	a9, a9

	/* Combine the shifted mantissa with the sign and exponent,
	   decrementing the exponent by a6.  (The exponent has already
	   been decremented by one due to the borrow from the subtraction,
	   but adding the mantissa will increment the exponent by one.)  */
	srli	xh, xh, 20
	sub	xh, xh, a6
	slli	xh, xh, 20
	add	xh, xh, a8
	j	.Lsub_round

.Lsub_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	xl, xl, 1
	slli	xl, xl, 1
	leaf_return

.Lsub_roundcarry:
	/* xl is always zero when the rounding increment overflows, so
	   there's no need to round it to an even value.  */
	addi	xh, xh, 1
	/* Overflow to the exponent is OK.  */
	leaf_return

.Lsub_xhzero:
	/* When normalizing the result, all the mantissa bits in the high
	   word are zero.  Shift by "20 + (leading zero count of xl) + 1".  */
	do_nsau	a6, xl, a7, a11
	addi	a6, a6, 21
	blt	a10, a6, .Lsub_subnormal

.Lsub_normalize_shift:
	bltui	a6, 32, .Lsub_shift_lt32

	ssl	a6
	src	a8, xl, a9
	sll	xl, a9
	movi	a9, 0

	srli	xh, xh, 20
	sub	xh, xh, a6
	slli	xh, xh, 20
	add	xh, xh, a8
	j	.Lsub_round

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

#endif /* L_addsubdf3 */

#ifdef L_muldf3

	/* Multiplication */
__muldf3_aux:

	/* Handle unusual cases (zeros, subnormals, NaNs and Infinities).
	   (This code is placed before the start of the function just to
	   keep it in range of the limited branch displacements.)  */

.Lmul_xexpzero:
	/* Clear the sign bit of x.  */
	slli	xh, xh, 1
	srli	xh, xh, 1

	/* If x is zero, return zero.  */
	or	a10, xh, xl
	beqz	a10, .Lmul_return_zero

	/* Normalize x.  Adjust the exponent in a8.  */
	beqz	xh, .Lmul_xh_zero
	do_nsau	a10, xh, a11, a12
	addi	a10, a10, -11
	ssl	a10
	src	xh, xh, xl
	sll	xl, xl
	movi	a8, 1
	sub	a8, a8, a10
	j	.Lmul_xnormalized	
.Lmul_xh_zero:
	do_nsau	a10, xl, a11, a12
	addi	a10, a10, -11
	movi	a8, -31
	sub	a8, a8, a10
	ssl	a10
	bltz	a10, .Lmul_xl_srl
	sll	xh, xl
	movi	xl, 0
	j	.Lmul_xnormalized
.Lmul_xl_srl:
	srl	xh, xl
	sll	xl, xl
	j	.Lmul_xnormalized
	
.Lmul_yexpzero:
	/* Clear the sign bit of y.  */
	slli	yh, yh, 1
	srli	yh, yh, 1

	/* If y is zero, return zero.  */
	or	a10, yh, yl
	beqz	a10, .Lmul_return_zero

	/* Normalize y.  Adjust the exponent in a9.  */
	beqz	yh, .Lmul_yh_zero
	do_nsau	a10, yh, a11, a12
	addi	a10, a10, -11
	ssl	a10
	src	yh, yh, yl
	sll	yl, yl
	movi	a9, 1
	sub	a9, a9, a10
	j	.Lmul_ynormalized	
.Lmul_yh_zero:
	do_nsau	a10, yl, a11, a12
	addi	a10, a10, -11
	movi	a9, -31
	sub	a9, a9, a10
	ssl	a10
	bltz	a10, .Lmul_yl_srl
	sll	yh, yl
	movi	yl, 0
	j	.Lmul_ynormalized
.Lmul_yl_srl:
	srl	yh, yl
	sll	yl, yl
	j	.Lmul_ynormalized	

.Lmul_return_zero:
	/* Return zero with the appropriate sign bit.  */
	srli	xh, a7, 31
	slli	xh, xh, 31
	movi	xl, 0
	j	.Lmul_done

.Lmul_xnan_or_inf:
	/* If y is zero, return NaN.  */
	bnez	yl, 1f
	slli	a8, yh, 1
	bnez	a8, 1f
	movi	a4, 0x80000	/* make it a quiet NaN */
	or	xh, xh, a4
	j	.Lmul_done
1:
	/* If y is NaN, return y.  */
	bnall	yh, a6, .Lmul_returnx
	slli	a8, yh, 12
	or	a8, a8, yl
	beqz	a8, .Lmul_returnx

.Lmul_returny:
	mov	xh, yh
	mov	xl, yl

.Lmul_returnx:
	/* Set the sign bit and return.  */
	extui	a7, a7, 31, 1
	slli	xh, xh, 1
	ssai	1
	src	xh, a7, xh
	j	.Lmul_done

.Lmul_ynan_or_inf:
	/* If x is zero, return NaN.  */
	bnez	xl, .Lmul_returny
	slli	a8, xh, 1
	bnez	a8, .Lmul_returny
	movi	a7, 0x80000	/* make it a quiet NaN */
	or	xh, yh, a7
	j	.Lmul_done

	.align	4
	.global	__muldf3
	.type	__muldf3, @function
__muldf3:
	leaf_entry sp, 32
#if __XTENSA_CALL0_ABI__
	addi	sp, sp, -32
	s32i	a12, sp, 16
	s32i	a13, sp, 20
	s32i	a14, sp, 24
	s32i	a15, sp, 28
#endif
	movi	a6, 0x7ff00000

	/* Get the sign of the result.  */
	xor	a7, xh, yh

	/* Check for NaN and infinity.  */
	ball	xh, a6, .Lmul_xnan_or_inf
	ball	yh, a6, .Lmul_ynan_or_inf

	/* Extract the exponents.  */
	extui	a8, xh, 20, 11
	extui	a9, yh, 20, 11

	beqz	a8, .Lmul_xexpzero
.Lmul_xnormalized:	
	beqz	a9, .Lmul_yexpzero
.Lmul_ynormalized:	

	/* Add the exponents.  */
	add	a8, a8, a9

	/* Replace sign/exponent fields with explicit "1.0".  */
	movi	a10, 0x1fffff
	or	xh, xh, a6
	and	xh, xh, a10
	or	yh, yh, a6
	and	yh, yh, a10

	/* Multiply 64x64 to 128 bits.  The result ends up in xh/xl/a6.
	   The least-significant word of the result is thrown away except
	   that if it is nonzero, the lsb of a6 is set to 1.  */
#if XCHAL_HAVE_MUL32_HIGH

	/* Compute a6 with any carry-outs in a10.  */
	movi	a10, 0
	mull	a6, xl, yh
	mull	a11, xh, yl
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a10, a10, 1
1:
	muluh	a11, xl, yl
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a10, a10, 1
1:	
	/* If the low word of the result is nonzero, set the lsb of a6.  */
	mull	a11, xl, yl
	beqz	a11, 1f
	movi	a9, 1
	or	a6, a6, a9
1:
	/* Compute xl with any carry-outs in a9.  */
	movi	a9, 0
	mull	a11, xh, yh
	add	a10, a10, a11
	bgeu	a10, a11, 1f
	addi	a9, a9, 1
1:	
	muluh	a11, xh, yl
	add	a10, a10, a11
	bgeu	a10, a11, 1f
	addi	a9, a9, 1
1:	
	muluh	xl, xl, yh
	add	xl, xl, a10
	bgeu	xl, a10, 1f
	addi	a9, a9, 1
1:
	/* Compute xh.  */
	muluh	xh, xh, yh
	add	xh, xh, a9

#else

	/* Break the inputs into 16-bit chunks and compute 16 32-bit partial
	   products.  These partial products are:

		0 xll * yll

		1 xll * ylh
		2 xlh * yll

		3 xll * yhl
		4 xlh * ylh
		5 xhl * yll

		6 xll * yhh
		7 xlh * yhl
		8 xhl * ylh
		9 xhh * yll

		10 xlh * yhh
		11 xhl * yhl
		12 xhh * ylh

		13 xhl * yhh
		14 xhh * yhl

		15 xhh * yhh

	   where the input chunks are (hh, hl, lh, ll).  If using the Mul16
	   or Mul32 multiplier options, these input chunks must be stored in
	   separate registers.  For Mac16, the UMUL.AA.* opcodes can specify
	   that the inputs come from either half of the registers, so there
	   is no need to shift them out ahead of time.  If there is no
	   multiply hardware, the 16-bit chunks can be extracted when setting
	   up the arguments to the separate multiply function.  */

	/* Save a7 since it is needed to hold a temporary value.  */
	s32i	a7, sp, 4
#if !XCHAL_HAVE_MUL16 && !XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MAC16
	/* Calling a separate multiply function will clobber a0 and requires
	   use of a8 as a temporary, so save those values now.  (The function
	   uses a custom ABI so nothing else needs to be saved.)  */
	s32i	a0, sp, 0
	s32i	a8, sp, 8
#endif

#if XCHAL_HAVE_MUL16 || XCHAL_HAVE_MUL32

#define xlh a12
#define ylh a13
#define xhh a14
#define yhh a15

	/* Get the high halves of the inputs into registers.  */
	srli	xlh, xl, 16
	srli	ylh, yl, 16
	srli	xhh, xh, 16
	srli	yhh, yh, 16

#define xll xl
#define yll yl
#define xhl xh
#define yhl yh

#if XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MUL16
	/* Clear the high halves of the inputs.  This does not matter
	   for MUL16 because the high bits are ignored.  */
	extui	xl, xl, 0, 16
	extui	xh, xh, 0, 16
	extui	yl, yl, 0, 16
	extui	yh, yh, 0, 16
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

	/* Add pp1 and pp2 into a10 with carry-out in a9.  */
	do_mul(a10, xl, l, yl, h)	/* pp 1 */
	do_mul(a11, xl, h, yl, l)	/* pp 2 */
	movi	a9, 0
	add	a10, a10, a11
	bgeu	a10, a11, 1f
	addi	a9, a9, 1
1:
	/* Initialize a6 with a9/a10 shifted into position.  Note that
	   this value can be safely incremented without any carry-outs.  */
	ssai	16
	src	a6, a9, a10

	/* Compute the low word into a10.  */
	do_mul(a11, xl, l, yl, l)	/* pp 0 */
	sll	a10, a10
	add	a10, a10, a11
	bgeu	a10, a11, 1f
	addi	a6, a6, 1
1:
	/* Compute the contributions of pp0-5 to a6, with carry-outs in a9.
	   This is good enough to determine the low half of a6, so that any
	   nonzero bits from the low word of the result can be collapsed
	   into a6, freeing up a register.  */
	movi	a9, 0
	do_mul(a11, xl, l, yh, l)	/* pp 3 */
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a9, a9, 1
1:
	do_mul(a11, xl, h, yl, h)	/* pp 4 */
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a9, a9, 1
1:
	do_mul(a11, xh, l, yl, l)	/* pp 5 */
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a9, a9, 1
1:
	/* Collapse any nonzero bits from the low word into a6.  */
	beqz	a10, 1f
	movi	a11, 1
	or	a6, a6, a11
1:
	/* Add pp6-9 into a11 with carry-outs in a10.  */
	do_mul(a7, xl, l, yh, h)	/* pp 6 */
	do_mul(a11, xh, h, yl, l)	/* pp 9 */
	movi	a10, 0
	add	a11, a11, a7
	bgeu	a11, a7, 1f
	addi	a10, a10, 1
1:	
	do_mul(a7, xl, h, yh, l)	/* pp 7 */
	add	a11, a11, a7
	bgeu	a11, a7, 1f
	addi	a10, a10, 1
1:	
	do_mul(a7, xh, l, yl, h)	/* pp 8 */
	add	a11, a11, a7
	bgeu	a11, a7, 1f
	addi	a10, a10, 1
1:	
	/* Shift a10/a11 into position, and add low half of a11 to a6.  */
	src	a10, a10, a11
	add	a10, a10, a9
	sll	a11, a11
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a10, a10, 1
1:
	/* Add pp10-12 into xl with carry-outs in a9.  */
	movi	a9, 0
	do_mul(xl, xl, h, yh, h)	/* pp 10 */
	add	xl, xl, a10
	bgeu	xl, a10, 1f
	addi	a9, a9, 1
1:
	do_mul(a10, xh, l, yh, l)	/* pp 11 */
	add	xl, xl, a10
	bgeu	xl, a10, 1f
	addi	a9, a9, 1
1:
	do_mul(a10, xh, h, yl, h)	/* pp 12 */
	add	xl, xl, a10
	bgeu	xl, a10, 1f
	addi	a9, a9, 1
1:
	/* Add pp13-14 into a11 with carry-outs in a10.  */
	do_mul(a11, xh, l, yh, h)	/* pp 13 */
	do_mul(a7, xh, h, yh, l)	/* pp 14 */
	movi	a10, 0
	add	a11, a11, a7
	bgeu	a11, a7, 1f
	addi	a10, a10, 1
1:
	/* Shift a10/a11 into position, and add low half of a11 to a6.  */
	src	a10, a10, a11
	add	a10, a10, a9
	sll	a11, a11
	add	xl, xl, a11
	bgeu	xl, a11, 1f
	addi	a10, a10, 1
1:
	/* Compute xh.  */
	do_mul(xh, xh, h, yh, h)	/* pp 15 */
	add	xh, xh, a10

	/* Restore values saved on the stack during the multiplication.  */
	l32i	a7, sp, 4
#if !XCHAL_HAVE_MUL16 && !XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MAC16
	l32i	a0, sp, 0
	l32i	a8, sp, 8
#endif
#endif

	/* Shift left by 12 bits, unless there was a carry-out from the
	   multiply, in which case, shift by 11 bits and increment the
	   exponent.  Note: It is convenient to use the constant 0x3ff
	   instead of 0x400 when removing the extra exponent bias (so that
	   it is easy to construct 0x7fe for the overflow check).  Reverse
	   the logic here to decrement the exponent sum by one unless there
	   was a carry-out.  */
	movi	a4, 11
	srli	a5, xh, 21 - 12
	bnez	a5, 1f
	addi	a4, a4, 1
	addi	a8, a8, -1
1:	ssl	a4
	src	xh, xh, xl
	src	xl, xl, a6
	sll	a6, a6

	/* Subtract the extra bias from the exponent sum (plus one to account
	   for the explicit "1.0" of the mantissa that will be added to the
	   exponent in the final result).  */
	movi	a4, 0x3ff
	sub	a8, a8, a4
	
	/* Check for over/underflow.  The value in a8 is one less than the
	   final exponent, so values in the range 0..7fd are OK here.  */
	slli	a4, a4, 1	/* 0x7fe */
	bgeu	a8, a4, .Lmul_overflow
	
.Lmul_round:
	/* Round.  */
	bgez	a6, .Lmul_rounded
	addi	xl, xl, 1
	beqz	xl, .Lmul_roundcarry
	slli	a6, a6, 1
	beqz	a6, .Lmul_exactlyhalf

.Lmul_rounded:
	/* Add the exponent to the mantissa.  */
	slli	a8, a8, 20
	add	xh, xh, a8

.Lmul_addsign:
	/* Add the sign bit.  */
	srli	a7, a7, 31
	slli	a7, a7, 31
	or	xh, xh, a7

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
	srli	xl, xl, 1
	slli	xl, xl, 1
	j	.Lmul_rounded

.Lmul_roundcarry:
	/* xl is always zero when the rounding increment overflows, so
	   there's no need to round it to an even value.  */
	addi	xh, xh, 1
	/* Overflow is OK -- it will be added to the exponent.  */
	j	.Lmul_rounded

.Lmul_overflow:
	bltz	a8, .Lmul_underflow
	/* Return +/- Infinity.  */
	addi	a8, a4, 1	/* 0x7ff */
	slli	xh, a8, 20
	movi	xl, 0
	j	.Lmul_addsign

.Lmul_underflow:
	/* Create a subnormal value, where the exponent field contains zero,
	   but the effective exponent is 1.  The value of a8 is one less than
	   the actual exponent, so just negate it to get the shift amount.  */
	neg	a8, a8
	mov	a9, a6
	ssr	a8
	bgeui	a8, 32, .Lmul_bigshift
	
	/* Shift xh/xl right.  Any bits that are shifted out of xl are saved
	   in a6 (combined with the shifted-out bits currently in a6) for
	   rounding the result.  */
	sll	a6, xl
	src	xl, xh, xl
	srl	xh, xh
	j	1f

.Lmul_bigshift:
	bgeui	a8, 64, .Lmul_flush_to_zero
	sll	a10, xl		/* lost bits shifted out of xl */
	src	a6, xh, xl
	srl	xl, xh
	movi	xh, 0
	or	a9, a9, a10

	/* Set the exponent to zero.  */
1:	movi	a8, 0

	/* Pack any nonzero bits shifted out into a6.  */
	beqz	a9, .Lmul_round
	movi	a9, 1
	or	a6, a6, a9
	j	.Lmul_round
	
.Lmul_flush_to_zero:
	/* Return zero with the appropriate sign bit.  */
	srli	xh, a7, 31
	slli	xh, xh, 31
	movi	xl, 0
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
#endif /* L_muldf3 */

#ifdef L_divdf3

	/* Division */
__divdf3_aux:

	/* Handle unusual cases (zeros, subnormals, NaNs and Infinities).
	   (This code is placed before the start of the function just to
	   keep it in range of the limited branch displacements.)  */

.Ldiv_yexpzero:
	/* Clear the sign bit of y.  */
	slli	yh, yh, 1
	srli	yh, yh, 1

	/* Check for division by zero.  */
	or	a10, yh, yl
	beqz	a10, .Ldiv_yzero

	/* Normalize y.  Adjust the exponent in a9.  */
	beqz	yh, .Ldiv_yh_zero
	do_nsau	a10, yh, a11, a9
	addi	a10, a10, -11
	ssl	a10
	src	yh, yh, yl
	sll	yl, yl
	movi	a9, 1
	sub	a9, a9, a10
	j	.Ldiv_ynormalized	
.Ldiv_yh_zero:
	do_nsau	a10, yl, a11, a9
	addi	a10, a10, -11
	movi	a9, -31
	sub	a9, a9, a10
	ssl	a10
	bltz	a10, .Ldiv_yl_srl
	sll	yh, yl
	movi	yl, 0
	j	.Ldiv_ynormalized
.Ldiv_yl_srl:
	srl	yh, yl
	sll	yl, yl
	j	.Ldiv_ynormalized	

.Ldiv_yzero:
	/* y is zero.  Return NaN if x is also zero; otherwise, infinity.  */
	slli	xh, xh, 1
	srli	xh, xh, 1
	or	xl, xl, xh
	srli	xh, a7, 31
	slli	xh, xh, 31
	or	xh, xh, a6
	bnez	xl, 1f
	movi	a4, 0x80000	/* make it a quiet NaN */
	or	xh, xh, a4
1:	movi	xl, 0
	leaf_return

.Ldiv_xexpzero:
	/* Clear the sign bit of x.  */
	slli	xh, xh, 1
	srli	xh, xh, 1

	/* If x is zero, return zero.  */
	or	a10, xh, xl
	beqz	a10, .Ldiv_return_zero

	/* Normalize x.  Adjust the exponent in a8.  */
	beqz	xh, .Ldiv_xh_zero
	do_nsau	a10, xh, a11, a8
	addi	a10, a10, -11
	ssl	a10
	src	xh, xh, xl
	sll	xl, xl
	movi	a8, 1
	sub	a8, a8, a10
	j	.Ldiv_xnormalized	
.Ldiv_xh_zero:
	do_nsau	a10, xl, a11, a8
	addi	a10, a10, -11
	movi	a8, -31
	sub	a8, a8, a10
	ssl	a10
	bltz	a10, .Ldiv_xl_srl
	sll	xh, xl
	movi	xl, 0
	j	.Ldiv_xnormalized
.Ldiv_xl_srl:
	srl	xh, xl
	sll	xl, xl
	j	.Ldiv_xnormalized
	
.Ldiv_return_zero:
	/* Return zero with the appropriate sign bit.  */
	srli	xh, a7, 31
	slli	xh, xh, 31
	movi	xl, 0
	leaf_return

.Ldiv_xnan_or_inf:
	/* Set the sign bit of the result.  */
	srli	a7, yh, 31
	slli	a7, a7, 31
	xor	xh, xh, a7
	/* If y is NaN or Inf, return NaN.  */
	bnall	yh, a6, 1f
	movi	a4, 0x80000	/* make it a quiet NaN */
	or	xh, xh, a4
1:	leaf_return

.Ldiv_ynan_or_inf:
	/* If y is Infinity, return zero.  */
	slli	a8, yh, 12
	or	a8, a8, yl
	beqz	a8, .Ldiv_return_zero
	/* y is NaN; return it.  */
	mov	xh, yh
	mov	xl, yl
	leaf_return

.Ldiv_highequal1:
	bltu	xl, yl, 2f
	j	3f

	.align	4
	.global	__divdf3
	.type	__divdf3, @function
__divdf3:
	leaf_entry sp, 16
	movi	a6, 0x7ff00000

	/* Get the sign of the result.  */
	xor	a7, xh, yh

	/* Check for NaN and infinity.  */
	ball	xh, a6, .Ldiv_xnan_or_inf
	ball	yh, a6, .Ldiv_ynan_or_inf

	/* Extract the exponents.  */
	extui	a8, xh, 20, 11
	extui	a9, yh, 20, 11

	beqz	a9, .Ldiv_yexpzero
.Ldiv_ynormalized:	
	beqz	a8, .Ldiv_xexpzero
.Ldiv_xnormalized:	

	/* Subtract the exponents.  */
	sub	a8, a8, a9

	/* Replace sign/exponent fields with explicit "1.0".  */
	movi	a10, 0x1fffff
	or	xh, xh, a6
	and	xh, xh, a10
	or	yh, yh, a6
	and	yh, yh, a10

	/* Set SAR for left shift by one.  */
	ssai	(32 - 1)

	/* The first digit of the mantissa division must be a one.
	   Shift x (and adjust the exponent) as needed to make this true.  */
	bltu	yh, xh, 3f
	beq	yh, xh, .Ldiv_highequal1
2:	src	xh, xh, xl
	sll	xl, xl
	addi	a8, a8, -1
3:
	/* Do the first subtraction and shift.  */
	sub	xh, xh, yh
	bgeu	xl, yl, 1f
	addi	xh, xh, -1
1:	sub	xl, xl, yl
	src	xh, xh, xl
	sll	xl, xl

	/* Put the quotient into a10/a11.  */
	movi	a10, 0
	movi	a11, 1

	/* Divide one bit at a time for 52 bits.  */
	movi	a9, 52
#if XCHAL_HAVE_LOOPS
	loop	a9, .Ldiv_loopend
#endif
.Ldiv_loop:
	/* Shift the quotient << 1.  */
	src	a10, a10, a11
	sll	a11, a11

	/* Is this digit a 0 or 1?  */
	bltu	xh, yh, 3f
	beq	xh, yh, .Ldiv_highequal2

	/* Output a 1 and subtract.  */
2:	addi	a11, a11, 1
	sub	xh, xh, yh
	bgeu	xl, yl, 1f
	addi	xh, xh, -1
1:	sub	xl, xl, yl

	/* Shift the dividend << 1.  */
3:	src	xh, xh, xl
	sll	xl, xl

#if !XCHAL_HAVE_LOOPS
	addi	a9, a9, -1
	bnez	a9, .Ldiv_loop
#endif
.Ldiv_loopend:

	/* Add the exponent bias (less one to account for the explicit "1.0"
	   of the mantissa that will be added to the exponent in the final
	   result).  */
	movi	a9, 0x3fe
	add	a8, a8, a9
	
	/* Check for over/underflow.  The value in a8 is one less than the
	   final exponent, so values in the range 0..7fd are OK here.  */
	addmi	a9, a9, 0x400	/* 0x7fe */
	bgeu	a8, a9, .Ldiv_overflow

.Ldiv_round:
	/* Round.  The remainder (<< 1) is in xh/xl.  */
	bltu	xh, yh, .Ldiv_rounded
	beq	xh, yh, .Ldiv_highequal3
.Ldiv_roundup:
	addi	a11, a11, 1
	beqz	a11, .Ldiv_roundcarry

.Ldiv_rounded:
	mov	xl, a11
	/* Add the exponent to the mantissa.  */
	slli	a8, a8, 20
	add	xh, a10, a8

.Ldiv_addsign:
	/* Add the sign bit.  */
	srli	a7, a7, 31
	slli	a7, a7, 31
	or	xh, xh, a7
	leaf_return

.Ldiv_highequal2:
	bgeu	xl, yl, 2b
	j	3b

.Ldiv_highequal3:
	bltu	xl, yl, .Ldiv_rounded
	bne	xl, yl, .Ldiv_roundup

	/* Remainder is exactly half the divisor.  Round even.  */
	addi	a11, a11, 1
	beqz	a11, .Ldiv_roundcarry
	srli	a11, a11, 1
	slli	a11, a11, 1
	j	.Ldiv_rounded

.Ldiv_overflow:
	bltz	a8, .Ldiv_underflow
	/* Return +/- Infinity.  */
	addi	a8, a9, 1	/* 0x7ff */
	slli	xh, a8, 20
	movi	xl, 0
	j	.Ldiv_addsign

.Ldiv_underflow:
	/* Create a subnormal value, where the exponent field contains zero,
	   but the effective exponent is 1.  The value of a8 is one less than
	   the actual exponent, so just negate it to get the shift amount.  */
	neg	a8, a8
	ssr	a8
	bgeui	a8, 32, .Ldiv_bigshift
	
	/* Shift a10/a11 right.  Any bits that are shifted out of a11 are
	   saved in a6 for rounding the result.  */
	sll	a6, a11
	src	a11, a10, a11
	srl	a10, a10
	j	1f

.Ldiv_bigshift:
	bgeui	a8, 64, .Ldiv_flush_to_zero
	sll	a9, a11		/* lost bits shifted out of a11 */
	src	a6, a10, a11
	srl	a11, a10
	movi	a10, 0
	or	xl, xl, a9

	/* Set the exponent to zero.  */
1:	movi	a8, 0

	/* Pack any nonzero remainder (in xh/xl) into a6.  */
	or	xh, xh, xl
	beqz	xh, 1f
	movi	a9, 1
	or	a6, a6, a9
	
	/* Round a10/a11 based on the bits shifted out into a6.  */
1:	bgez	a6, .Ldiv_rounded
	addi	a11, a11, 1
	beqz	a11, .Ldiv_roundcarry
	slli	a6, a6, 1
	bnez	a6, .Ldiv_rounded
	srli	a11, a11, 1
	slli	a11, a11, 1
	j	.Ldiv_rounded

.Ldiv_roundcarry:
	/* a11 is always zero when the rounding increment overflows, so
	   there's no need to round it to an even value.  */
	addi	a10, a10, 1
	/* Overflow to the exponent field is OK.  */
	j	.Ldiv_rounded

.Ldiv_flush_to_zero:
	/* Return zero with the appropriate sign bit.  */
	srli	xh, a7, 31
	slli	xh, xh, 31
	movi	xl, 0
	leaf_return

#endif /* L_divdf3 */

#ifdef L_cmpdf2

	/* Equal and Not Equal */

	.align	4
	.global	__eqdf2
	.global	__nedf2
	.set	__nedf2, __eqdf2
	.type	__eqdf2, @function
__eqdf2:
	leaf_entry sp, 16
	bne	xl, yl, 2f
	bne	xh, yh, 4f

	/* The values are equal but NaN != NaN.  Check the exponent.  */
	movi	a6, 0x7ff00000
	ball	xh, a6, 3f

	/* Equal.  */
	movi	a2, 0
	leaf_return

	/* Not equal.  */
2:	movi	a2, 1
	leaf_return

	/* Check if the mantissas are nonzero.  */
3:	slli	a7, xh, 12
	or	a7, a7, xl
	j	5f

	/* Check if x and y are zero with different signs.  */
4:	or	a7, xh, yh
	slli	a7, a7, 1
	or	a7, a7, xl	/* xl == yl here */

	/* Equal if a7 == 0, where a7 is either abs(x | y) or the mantissa
	   or x when exponent(x) = 0x7ff and x == y.  */
5:	movi	a2, 0
	movi	a3, 1
	movnez	a2, a3, a7	
	leaf_return


	/* Greater Than */

	.align	4
	.global	__gtdf2
	.type	__gtdf2, @function
__gtdf2:
	leaf_entry sp, 16
	movi	a6, 0x7ff00000
	ball	xh, a6, 2f
1:	bnall	yh, a6, .Lle_cmp

	/* Check if y is a NaN.  */
	slli	a7, yh, 12
	or	a7, a7, yl
	beqz	a7, .Lle_cmp
	movi	a2, 0
	leaf_return

	/* Check if x is a NaN.  */
2:	slli	a7, xh, 12
	or	a7, a7, xl
	beqz	a7, 1b
	movi	a2, 0
	leaf_return


	/* Less Than or Equal */

	.align	4
	.global	__ledf2
	.type	__ledf2, @function
__ledf2:
	leaf_entry sp, 16
	movi	a6, 0x7ff00000
	ball	xh, a6, 2f
1:	bnall	yh, a6, .Lle_cmp

	/* Check if y is a NaN.  */
	slli	a7, yh, 12
	or	a7, a7, yl
	beqz	a7, .Lle_cmp
	movi	a2, 1
	leaf_return

	/* Check if x is a NaN.  */
2:	slli	a7, xh, 12
	or	a7, a7, xl
	beqz	a7, 1b
	movi	a2, 1
	leaf_return

.Lle_cmp:
	/* Check if x and y have different signs.  */
	xor	a7, xh, yh
	bltz	a7, .Lle_diff_signs

	/* Check if x is negative.  */
	bltz	xh, .Lle_xneg

	/* Check if x <= y.  */
	bltu	xh, yh, 4f
	bne	xh, yh, 5f
	bltu	yl, xl, 5f
4:	movi	a2, 0
	leaf_return

.Lle_xneg:
	/* Check if y <= x.  */
	bltu	yh, xh, 4b
	bne	yh, xh, 5f
	bgeu	xl, yl, 4b
5:	movi	a2, 1
	leaf_return

.Lle_diff_signs:
	bltz	xh, 4b

	/* Check if both x and y are zero.  */
	or	a7, xh, yh
	slli	a7, a7, 1
	or	a7, a7, xl
	or	a7, a7, yl
	movi	a2, 1
	movi	a3, 0
	moveqz	a2, a3, a7
	leaf_return


	/* Greater Than or Equal */

	.align	4
	.global	__gedf2
	.type	__gedf2, @function
__gedf2:
	leaf_entry sp, 16
	movi	a6, 0x7ff00000
	ball	xh, a6, 2f
1:	bnall	yh, a6, .Llt_cmp

	/* Check if y is a NaN.  */
	slli	a7, yh, 12
	or	a7, a7, yl
	beqz	a7, .Llt_cmp
	movi	a2, -1
	leaf_return

	/* Check if x is a NaN.  */
2:	slli	a7, xh, 12
	or	a7, a7, xl
	beqz	a7, 1b
	movi	a2, -1
	leaf_return


	/* Less Than */

	.align	4
	.global	__ltdf2
	.type	__ltdf2, @function
__ltdf2:
	leaf_entry sp, 16
	movi	a6, 0x7ff00000
	ball	xh, a6, 2f
1:	bnall	yh, a6, .Llt_cmp

	/* Check if y is a NaN.  */
	slli	a7, yh, 12
	or	a7, a7, yl
	beqz	a7, .Llt_cmp
	movi	a2, 0
	leaf_return

	/* Check if x is a NaN.  */
2:	slli	a7, xh, 12
	or	a7, a7, xl
	beqz	a7, 1b
	movi	a2, 0
	leaf_return

.Llt_cmp:
	/* Check if x and y have different signs.  */
	xor	a7, xh, yh
	bltz	a7, .Llt_diff_signs

	/* Check if x is negative.  */
	bltz	xh, .Llt_xneg

	/* Check if x < y.  */
	bltu	xh, yh, 4f
	bne	xh, yh, 5f
	bgeu	xl, yl, 5f
4:	movi	a2, -1
	leaf_return

.Llt_xneg:
	/* Check if y < x.  */
	bltu	yh, xh, 4b
	bne	yh, xh, 5f
	bltu	yl, xl, 4b
5:	movi	a2, 0
	leaf_return

.Llt_diff_signs:
	bgez	xh, 5b

	/* Check if both x and y are nonzero.  */
	or	a7, xh, yh
	slli	a7, a7, 1
	or	a7, a7, xl
	or	a7, a7, yl
	movi	a2, 0
	movi	a3, -1
	movnez	a2, a3, a7
	leaf_return


	/* Unordered */

	.align	4
	.global	__unorddf2
	.type	__unorddf2, @function
__unorddf2:
	leaf_entry sp, 16
	movi	a6, 0x7ff00000
	ball	xh, a6, 3f
1:	ball	yh, a6, 4f
2:	movi	a2, 0
	leaf_return

3:	slli	a7, xh, 12
	or	a7, a7, xl
	beqz	a7, 1b
	movi	a2, 1
	leaf_return

4:	slli	a7, yh, 12
	or	a7, a7, yl
	beqz	a7, 2b
	movi	a2, 1
	leaf_return

#endif /* L_cmpdf2 */

#ifdef L_fixdfsi

	.align	4
	.global	__fixdfsi
	.type	__fixdfsi, @function
__fixdfsi:
	leaf_entry sp, 16

	/* Check for NaN and Infinity.  */
	movi	a6, 0x7ff00000
	ball	xh, a6, .Lfixdfsi_nan_or_inf

	/* Extract the exponent and check if 0 < (exp - 0x3fe) < 32.  */
	extui	a4, xh, 20, 11
	extui	a5, a6, 19, 10	/* 0x3fe */
	sub	a4, a4, a5
	bgei	a4, 32, .Lfixdfsi_maxint
	blti	a4, 1, .Lfixdfsi_zero

	/* Add explicit "1.0" and shift << 11.  */
	or	a7, xh, a6
	ssai	(32 - 11)
	src	a5, a7, xl

	/* Shift back to the right, based on the exponent.  */
	ssl	a4		/* shift by 32 - a4 */
	srl	a5, a5

	/* Negate the result if sign != 0.  */
	neg	a2, a5
	movgez	a2, a5, a7
	leaf_return

.Lfixdfsi_nan_or_inf:
	/* Handle Infinity and NaN.  */
	slli	a4, xh, 12
	or	a4, a4, xl
	beqz	a4, .Lfixdfsi_maxint

	/* Translate NaN to +maxint.  */
	movi	xh, 0

.Lfixdfsi_maxint:
	slli	a4, a6, 11	/* 0x80000000 */
	addi	a5, a4, -1	/* 0x7fffffff */
	movgez	a4, a5, xh
	mov	a2, a4
	leaf_return

.Lfixdfsi_zero:
	movi	a2, 0
	leaf_return

#endif /* L_fixdfsi */

#ifdef L_fixdfdi

	.align	4
	.global	__fixdfdi
	.type	__fixdfdi, @function
__fixdfdi:
	leaf_entry sp, 16

	/* Check for NaN and Infinity.  */
	movi	a6, 0x7ff00000
	ball	xh, a6, .Lfixdfdi_nan_or_inf

	/* Extract the exponent and check if 0 < (exp - 0x3fe) < 64.  */
	extui	a4, xh, 20, 11
	extui	a5, a6, 19, 10	/* 0x3fe */
	sub	a4, a4, a5
	bgei	a4, 64, .Lfixdfdi_maxint
	blti	a4, 1, .Lfixdfdi_zero

	/* Add explicit "1.0" and shift << 11.  */
	or	a7, xh, a6
	ssai	(32 - 11)
	src	xh, a7, xl
	sll	xl, xl

	/* Shift back to the right, based on the exponent.  */
	ssl	a4		/* shift by 64 - a4 */
	bgei	a4, 32, .Lfixdfdi_smallshift
	srl	xl, xh
	movi	xh, 0

.Lfixdfdi_shifted:	
	/* Negate the result if sign != 0.  */
	bgez	a7, 1f
	neg	xl, xl
	neg	xh, xh
	beqz	xl, 1f
	addi	xh, xh, -1
1:	leaf_return

.Lfixdfdi_smallshift:
	src	xl, xh, xl
	srl	xh, xh
	j	.Lfixdfdi_shifted

.Lfixdfdi_nan_or_inf:
	/* Handle Infinity and NaN.  */
	slli	a4, xh, 12
	or	a4, a4, xl
	beqz	a4, .Lfixdfdi_maxint

	/* Translate NaN to +maxint.  */
	movi	xh, 0

.Lfixdfdi_maxint:
	slli	a7, a6, 11	/* 0x80000000 */
	bgez	xh, 1f
	mov	xh, a7
	movi	xl, 0
	leaf_return

1:	addi	xh, a7, -1	/* 0x7fffffff */
	movi	xl, -1
	leaf_return

.Lfixdfdi_zero:
	movi	xh, 0
	movi	xl, 0
	leaf_return

#endif /* L_fixdfdi */

#ifdef L_fixunsdfsi

	.align	4
	.global	__fixunsdfsi
	.type	__fixunsdfsi, @function
__fixunsdfsi:
	leaf_entry sp, 16

	/* Check for NaN and Infinity.  */
	movi	a6, 0x7ff00000
	ball	xh, a6, .Lfixunsdfsi_nan_or_inf

	/* Extract the exponent and check if 0 <= (exp - 0x3ff) < 32.  */
	extui	a4, xh, 20, 11
	extui	a5, a6, 20, 10	/* 0x3ff */
	sub	a4, a4, a5
	bgei	a4, 32, .Lfixunsdfsi_maxint
	bltz	a4, .Lfixunsdfsi_zero

	/* Add explicit "1.0" and shift << 11.  */
	or	a7, xh, a6
	ssai	(32 - 11)
	src	a5, a7, xl

	/* Shift back to the right, based on the exponent.  */
	addi	a4, a4, 1
	beqi	a4, 32, .Lfixunsdfsi_bigexp
	ssl	a4		/* shift by 32 - a4 */
	srl	a5, a5

	/* Negate the result if sign != 0.  */
	neg	a2, a5
	movgez	a2, a5, a7
	leaf_return

.Lfixunsdfsi_nan_or_inf:
	/* Handle Infinity and NaN.  */
	slli	a4, xh, 12
	or	a4, a4, xl
	beqz	a4, .Lfixunsdfsi_maxint

	/* Translate NaN to 0xffffffff.  */
	movi	a2, -1
	leaf_return

.Lfixunsdfsi_maxint:
	slli	a4, a6, 11	/* 0x80000000 */
	movi	a5, -1		/* 0xffffffff */
	movgez	a4, a5, xh
	mov	a2, a4
	leaf_return

.Lfixunsdfsi_zero:
	movi	a2, 0
	leaf_return

.Lfixunsdfsi_bigexp:
	/* Handle unsigned maximum exponent case.  */
	bltz	xh, 1f
	mov	a2, a5		/* no shift needed */
	leaf_return

	/* Return 0x80000000 if negative.  */
1:	slli	a2, a6, 11
	leaf_return

#endif /* L_fixunsdfsi */

#ifdef L_fixunsdfdi

	.align	4
	.global	__fixunsdfdi
	.type	__fixunsdfdi, @function
__fixunsdfdi:
	leaf_entry sp, 16

	/* Check for NaN and Infinity.  */
	movi	a6, 0x7ff00000
	ball	xh, a6, .Lfixunsdfdi_nan_or_inf

	/* Extract the exponent and check if 0 <= (exp - 0x3ff) < 64.  */
	extui	a4, xh, 20, 11
	extui	a5, a6, 20, 10	/* 0x3ff */
	sub	a4, a4, a5
	bgei	a4, 64, .Lfixunsdfdi_maxint
	bltz	a4, .Lfixunsdfdi_zero

	/* Add explicit "1.0" and shift << 11.  */
	or	a7, xh, a6
	ssai	(32 - 11)
	src	xh, a7, xl
	sll	xl, xl

	/* Shift back to the right, based on the exponent.  */
	addi	a4, a4, 1
	beqi	a4, 64, .Lfixunsdfdi_bigexp
	ssl	a4		/* shift by 64 - a4 */
	bgei	a4, 32, .Lfixunsdfdi_smallshift
	srl	xl, xh
	movi	xh, 0

.Lfixunsdfdi_shifted:
	/* Negate the result if sign != 0.  */
	bgez	a7, 1f
	neg	xl, xl
	neg	xh, xh
	beqz	xl, 1f
	addi	xh, xh, -1
1:	leaf_return

.Lfixunsdfdi_smallshift:
	src	xl, xh, xl
	srl	xh, xh
	j	.Lfixunsdfdi_shifted

.Lfixunsdfdi_nan_or_inf:
	/* Handle Infinity and NaN.  */
	slli	a4, xh, 12
	or	a4, a4, xl
	beqz	a4, .Lfixunsdfdi_maxint

	/* Translate NaN to 0xffffffff.... */
1:	movi	xh, -1
	movi	xl, -1
	leaf_return

.Lfixunsdfdi_maxint:
	bgez	xh, 1b
2:	slli	xh, a6, 11	/* 0x80000000 */
	movi	xl, 0
	leaf_return

.Lfixunsdfdi_zero:
	movi	xh, 0
	movi	xl, 0
	leaf_return

.Lfixunsdfdi_bigexp:
	/* Handle unsigned maximum exponent case.  */
	bltz	a7, 2b
	leaf_return		/* no shift needed */

#endif /* L_fixunsdfdi */

#ifdef L_floatsidf

	.align	4
	.global	__floatunsidf
	.type	__floatunsidf, @function
__floatunsidf:
	leaf_entry sp, 16
	beqz	a2, .Lfloatsidf_return_zero

	/* Set the sign to zero and jump to the floatsidf code.  */
	movi	a7, 0
	j	.Lfloatsidf_normalize

	.align	4
	.global	__floatsidf
	.type	__floatsidf, @function
__floatsidf:
	leaf_entry sp, 16

	/* Check for zero.  */
	beqz	a2, .Lfloatsidf_return_zero

	/* Save the sign.  */
	extui	a7, a2, 31, 1

	/* Get the absolute value.  */
#if XCHAL_HAVE_ABS
	abs	a2, a2
#else
	neg	a4, a2
	movltz	a2, a4, a2
#endif

.Lfloatsidf_normalize:
	/* Normalize with the first 1 bit in the msb.  */
	do_nsau	a4, a2, a5, a6
	ssl	a4
	sll	a5, a2

	/* Shift the mantissa into position.  */
	srli	xh, a5, 11
	slli	xl, a5, (32 - 11)

	/* Set the exponent.  */
	movi	a5, 0x41d	/* 0x3fe + 31 */
	sub	a5, a5, a4
	slli	a5, a5, 20
	add	xh, xh, a5

	/* Add the sign and return. */
	slli	a7, a7, 31
	or	xh, xh, a7
	leaf_return

.Lfloatsidf_return_zero:
	movi	a3, 0
	leaf_return

#endif /* L_floatsidf */

#ifdef L_floatdidf

	.align	4
	.global	__floatundidf
	.type	__floatundidf, @function
__floatundidf:
	leaf_entry sp, 16

	/* Check for zero.  */
	or	a4, xh, xl
	beqz	a4, 2f

	/* Set the sign to zero and jump to the floatdidf code.  */
	movi	a7, 0
	j	.Lfloatdidf_normalize

	.align	4
	.global	__floatdidf
	.type	__floatdidf, @function
__floatdidf:
	leaf_entry sp, 16

	/* Check for zero.  */
	or	a4, xh, xl
	beqz	a4, 2f

	/* Save the sign.  */
	extui	a7, xh, 31, 1

	/* Get the absolute value.  */
	bgez	xh, .Lfloatdidf_normalize
	neg	xl, xl
	neg	xh, xh
	beqz	xl, .Lfloatdidf_normalize
	addi	xh, xh, -1

.Lfloatdidf_normalize:
	/* Normalize with the first 1 bit in the msb of xh.  */
	beqz	xh, .Lfloatdidf_bigshift
	do_nsau	a4, xh, a5, a6
	ssl	a4
	src	xh, xh, xl
	sll	xl, xl

.Lfloatdidf_shifted:
	/* Shift the mantissa into position, with rounding bits in a6.  */
	ssai	11
	sll	a6, xl
	src	xl, xh, xl
	srl	xh, xh

	/* Set the exponent.  */
	movi	a5, 0x43d	/* 0x3fe + 63 */
	sub	a5, a5, a4
	slli	a5, a5, 20
	add	xh, xh, a5

	/* Add the sign.  */
	slli	a7, a7, 31
	or	xh, xh, a7

	/* Round up if the leftover fraction is >= 1/2.  */
	bgez	a6, 2f
	addi	xl, xl, 1
	beqz	xl, .Lfloatdidf_roundcarry

	/* Check if the leftover fraction is exactly 1/2.  */
	slli	a6, a6, 1
	beqz	a6, .Lfloatdidf_exactlyhalf
2:	leaf_return

.Lfloatdidf_bigshift:
	/* xh is zero.  Normalize with first 1 bit of xl in the msb of xh.  */
	do_nsau	a4, xl, a5, a6
	ssl	a4
	sll	xh, xl
	movi	xl, 0
	addi	a4, a4, 32
	j	.Lfloatdidf_shifted

.Lfloatdidf_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	xl, xl, 1
	slli	xl, xl, 1
	leaf_return

.Lfloatdidf_roundcarry:
	/* xl is always zero when the rounding increment overflows, so
	   there's no need to round it to an even value.  */
	addi	xh, xh, 1
	/* Overflow to the exponent is OK.  */
	leaf_return

#endif /* L_floatdidf */

#ifdef L_truncdfsf2

	.align	4
	.global	__truncdfsf2
	.type	__truncdfsf2, @function
__truncdfsf2:
	leaf_entry sp, 16

	/* Adjust the exponent bias.  */
	movi	a4, (0x3ff - 0x7f) << 20
	sub	a5, xh, a4

	/* Check for underflow.  */
	xor	a6, xh, a5
	bltz	a6, .Ltrunc_underflow
	extui	a6, a5, 20, 11
	beqz	a6, .Ltrunc_underflow

	/* Check for overflow.  */
	movi	a4, 255
	bge	a6, a4, .Ltrunc_overflow

	/* Shift a5/xl << 3 into a5/a4.  */
	ssai	(32 - 3)
	src	a5, a5, xl
	sll	a4, xl

.Ltrunc_addsign:
	/* Add the sign bit.  */
	extui	a6, xh, 31, 1
	slli	a6, a6, 31
	or	a2, a6, a5

	/* Round up if the leftover fraction is >= 1/2.  */
	bgez	a4, 1f
	addi	a2, a2, 1
	/* Overflow to the exponent is OK.  The answer will be correct.  */

	/* Check if the leftover fraction is exactly 1/2.  */
	slli	a4, a4, 1
	beqz	a4, .Ltrunc_exactlyhalf
1:	leaf_return

.Ltrunc_exactlyhalf:
	/* Round down to the nearest even value.  */
	srli	a2, a2, 1
	slli	a2, a2, 1
	leaf_return

.Ltrunc_overflow:
	/* Check if exponent == 0x7ff.  */
	movi	a4, 0x7ff00000
	bnall	xh, a4, 1f

	/* Check if mantissa is nonzero.  */
	slli	a5, xh, 12
	or	a5, a5, xl
	beqz	a5, 1f

	/* Shift a4 to set a bit in the mantissa, making a quiet NaN.  */
	srli	a4, a4, 1

1:	slli	a4, a4, 4	/* 0xff000000 or 0xff800000 */
	/* Add the sign bit.  */
	extui	a6, xh, 31, 1
	ssai	1
	src	a2, a6, a4
	leaf_return

.Ltrunc_underflow:
	/* Find shift count for a subnormal.  Flush to zero if >= 32.  */
	extui	a6, xh, 20, 11
	movi	a5, 0x3ff - 0x7f
	sub	a6, a5, a6
	addi	a6, a6, 1
	bgeui	a6, 32, 1f

	/* Replace the exponent with an explicit "1.0".  */
	slli	a5, a5, 13	/* 0x700000 */
	or	a5, a5, xh
	slli	a5, a5, 11
	srli	a5, a5, 11

	/* Shift the mantissa left by 3 bits (into a5/a4).  */
	ssai	(32 - 3)
	src	a5, a5, xl
	sll	a4, xl

	/* Shift right by a6.  */
	ssr	a6
	sll	a7, a4
	src	a4, a5, a4
	srl	a5, a5
	beqz	a7, .Ltrunc_addsign
	or	a4, a4, a6	/* any positive, nonzero value will work */
	j	.Ltrunc_addsign

	/* Return +/- zero.  */
1:	extui	a2, xh, 31, 1
	slli	a2, a2, 31
	leaf_return

#endif /* L_truncdfsf2 */

#ifdef L_extendsfdf2

	.align	4
	.global	__extendsfdf2
	.type	__extendsfdf2, @function
__extendsfdf2:
	leaf_entry sp, 16

	/* Save the sign bit and then shift it off.  */
	extui	a5, a2, 31, 1
	slli	a5, a5, 31
	slli	a4, a2, 1

	/* Extract and check the exponent.  */
	extui	a6, a2, 23, 8
	beqz	a6, .Lextend_expzero
	addi	a6, a6, 1
	beqi	a6, 256, .Lextend_nan_or_inf

	/* Shift >> 3 into a4/xl.  */
	srli	a4, a4, 4
	slli	xl, a2, (32 - 3)

	/* Adjust the exponent bias.  */
	movi	a6, (0x3ff - 0x7f) << 20
	add	a4, a4, a6

	/* Add the sign bit.  */
	or	xh, a4, a5
	leaf_return

.Lextend_nan_or_inf:
	movi	a4, 0x7ff00000

	/* Check for NaN.  */
	slli	a7, a2, 9
	beqz	a7, 1f

	slli	a6, a6, 11	/* 0x80000 */
	or	a4, a4, a6

	/* Add the sign and return.  */
1:	or	xh, a4, a5
	movi	xl, 0
	leaf_return

.Lextend_expzero:
	beqz	a4, 1b

	/* Normalize it to have 8 zero bits before the first 1 bit.  */
	do_nsau	a7, a4, a2, a3
	addi	a7, a7, -8
	ssl	a7
	sll	a4, a4
	
	/* Shift >> 3 into a4/xl.  */
	slli	xl, a4, (32 - 3)
	srli	a4, a4, 3

	/* Set the exponent.  */
	movi	a6, 0x3fe - 0x7f
	sub	a6, a6, a7
	slli	a6, a6, 20
	add	a4, a4, a6

	/* Add the sign and return.  */
	or	xh, a4, a5
	leaf_return

#endif /* L_extendsfdf2 */


