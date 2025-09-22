/* This is an assembly language implementation of mulsi3, divsi3, and modsi3
   for the sparc processor.

   These routines are derived from the SPARC Architecture Manual, version 8,
   slightly edited to match the desired calling convention, and also to
   optimize them for our purposes.  */

#ifdef L_mulsi3
.text
	.align 4
	.global .umul
	.proc 4
.umul:
	or	%o0, %o1, %o4	! logical or of multiplier and multiplicand
	mov	%o0, %y		! multiplier to Y register
	andncc	%o4, 0xfff, %o5	! mask out lower 12 bits
	be	mul_shortway	! can do it the short way
	andcc	%g0, %g0, %o4	! zero the partial product and clear NV cc
	!
	! long multiply
	!
	mulscc	%o4, %o1, %o4	! first iteration of 33
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4	! 32nd iteration
	mulscc	%o4, %g0, %o4	! last iteration only shifts
	! the upper 32 bits of product are wrong, but we do not care
	retl
	rd	%y, %o0
	!
	! short multiply
	!
mul_shortway:
	mulscc	%o4, %o1, %o4	! first iteration of 13
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4
	mulscc	%o4, %o1, %o4	! 12th iteration
	mulscc	%o4, %g0, %o4	! last iteration only shifts
	rd	%y, %o5
	sll	%o4, 12, %o4	! left shift partial product by 12 bits
	srl	%o5, 20, %o5	! right shift partial product by 20 bits
	retl
	or	%o5, %o4, %o0	! merge for true product
#endif

#ifdef L_divsi3
/*
 * Division and remainder, from Appendix E of the SPARC Version 8
 * Architecture Manual, with fixes from Gordon Irlam.
 */

/*
 * Input: dividend and divisor in %o0 and %o1 respectively.
 *
 * m4 parameters:
 *  .div	name of function to generate
 *  div		div=div => %o0 / %o1; div=rem => %o0 % %o1
 *  true		true=true => signed; true=false => unsigned
 *
 * Algorithm parameters:
 *  N		how many bits per iteration we try to get (4)
 *  WORDSIZE	total number of bits (32)
 *
 * Derived constants:
 *  TOPBITS	number of bits in the top decade of a number
 *
 * Important variables:
 *  Q		the partial quotient under development (initially 0)
 *  R		the remainder so far, initially the dividend
 *  ITER	number of main division loop iterations required;
 *		equal to ceil(log2(quotient) / N).  Note that this
 *		is the log base (2^N) of the quotient.
 *  V		the current comparand, initially divisor*2^(ITER*N-1)
 *
 * Cost:
 *  Current estimate for non-large dividend is
 *	ceil(log2(quotient) / N) * (10 + 7N/2) + C
 *  A large dividend is one greater than 2^(31-TOPBITS) and takes a
 *  different path, as the upper bits of the quotient must be developed
 *  one bit at a time.
 */
        .global .udiv
        .align 4
        .proc 4
        .text
.udiv:
         b ready_to_divide
         mov 0, %g3             ! result is always positive

        .global .div
        .align 4
        .proc 4
        .text
.div:
	! compute sign of result; if neither is negative, no problem
	orcc	%o1, %o0, %g0	! either negative?
	bge	ready_to_divide	! no, go do the divide
	xor	%o1, %o0, %g3	! compute sign in any case
	tst	%o1
	bge	1f
	tst	%o0
	! %o1 is definitely negative; %o0 might also be negative
	bge	ready_to_divide	! if %o0 not negative...
	sub	%g0, %o1, %o1	! in any case, make %o1 nonneg
1:	! %o0 is negative, %o1 is nonnegative
	sub	%g0, %o0, %o0	! make %o0 nonnegative


ready_to_divide:

	! Ready to divide.  Compute size of quotient; scale comparand.
	orcc	%o1, %g0, %o5
	bne	1f
	mov	%o0, %o3

	! Divide by zero trap.  If it returns, return 0 (about as
	! wrong as possible, but that is what SunOS does...).
	ta	0x2    		! ST_DIV0
	retl
	clr	%o0

1:
	cmp	%o3, %o5		! if %o1 exceeds %o0, done
	blu	got_result		! (and algorithm fails otherwise)
	clr	%o2
	sethi	%hi(1 << (32 - 4 - 1)), %g1
	cmp	%o3, %g1
	blu	not_really_big
	clr	%o4

	! Here the dividend is >= 2**(31-N) or so.  We must be careful here,
	! as our usual N-at-a-shot divide step will cause overflow and havoc.
	! The number of bits in the result here is N*ITER+SC, where SC <= N.
	! Compute ITER in an unorthodox manner: know we need to shift V into
	! the top decade: so do not even bother to compare to R.
	1:
		cmp	%o5, %g1
		bgeu	3f
		mov	1, %g2
		sll	%o5, 4, %o5
		b	1b
		add	%o4, 1, %o4

	! Now compute %g2.
	2:	addcc	%o5, %o5, %o5
		bcc	not_too_big
		add	%g2, 1, %g2

		! We get here if the %o1 overflowed while shifting.
		! This means that %o3 has the high-order bit set.
		! Restore %o5 and subtract from %o3.
		sll	%g1, 4, %g1	! high order bit
		srl	%o5, 1, %o5	! rest of %o5
		add	%o5, %g1, %o5
		b	do_single_div
		sub	%g2, 1, %g2

	not_too_big:
	3:	cmp	%o5, %o3
		blu	2b
		nop
		be	do_single_div
		nop
	/* NB: these are commented out in the V8-SPARC manual as well */
	/* (I do not understand this) */
	! %o5 > %o3: went too far: back up 1 step
	!	srl	%o5, 1, %o5
	!	dec	%g2
	! do single-bit divide steps
	!
	! We have to be careful here.  We know that %o3 >= %o5, so we can do the
	! first divide step without thinking.  BUT, the others are conditional,
	! and are only done if %o3 >= 0.  Because both %o3 and %o5 may have the high-
	! order bit set in the first step, just falling into the regular
	! division loop will mess up the first time around.
	! So we unroll slightly...
	do_single_div:
		subcc	%g2, 1, %g2
		bl	end_regular_divide
		nop
		sub	%o3, %o5, %o3
		mov	1, %o2
		b	end_single_divloop
		nop
	single_divloop:
		sll	%o2, 1, %o2
		bl	1f
		srl	%o5, 1, %o5
		! %o3 >= 0
		sub	%o3, %o5, %o3
		b	2f
		add	%o2, 1, %o2
	1:	! %o3 < 0
		add	%o3, %o5, %o3
		sub	%o2, 1, %o2
	2:
	end_single_divloop:
		subcc	%g2, 1, %g2
		bge	single_divloop
		tst	%o3
		b,a	end_regular_divide

not_really_big:
1:
	sll	%o5, 4, %o5
	cmp	%o5, %o3
	bleu	1b
	addcc	%o4, 1, %o4
	be	got_result
	sub	%o4, 1, %o4

	tst	%o3	! set up for initial iteration
divloop:
	sll	%o2, 4, %o2
	! depth 1, accumulated bits 0
	bl	L1.16
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 2, accumulated bits 1
	bl	L2.17
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 3, accumulated bits 3
	bl	L3.19
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 4, accumulated bits 7
	bl	L4.23
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (7*2+1), %o2
	
L4.23:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (7*2-1), %o2
	
	
L3.19:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 4, accumulated bits 5
	bl	L4.21
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (5*2+1), %o2
	
L4.21:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (5*2-1), %o2
	
L2.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 3, accumulated bits 1
	bl	L3.17
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 4, accumulated bits 3
	bl	L4.19
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (3*2+1), %o2
	
L4.19:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (3*2-1), %o2

L3.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 4, accumulated bits 1
	bl	L4.17
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (1*2+1), %o2

L4.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (1*2-1), %o2
	
L1.16:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 2, accumulated bits -1
	bl	L2.15
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 3, accumulated bits -1
	bl	L3.15
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 4, accumulated bits -1
	bl	L4.15
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-1*2+1), %o2
	
L4.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-1*2-1), %o2
	
L3.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 4, accumulated bits -3
	bl	L4.13
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-3*2+1), %o2
	
L4.13:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-3*2-1), %o2
	
L2.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 3, accumulated bits -3
	bl	L3.13
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 4, accumulated bits -5
	bl	L4.11
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-5*2+1), %o2
	
L4.11:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-5*2-1), %o2
	
L3.13:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 4, accumulated bits -7
	bl	L4.9
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-7*2+1), %o2

L4.9:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-7*2-1), %o2
	
	9:
end_regular_divide:
	subcc	%o4, 1, %o4
	bge	divloop
	tst	%o3
	bl,a	got_result
	! non-restoring fixup here (one instruction only!)
	sub	%o2, 1, %o2


got_result:
	! check to see if answer should be < 0
	tst	%g3
	bl,a	1f
	sub %g0, %o2, %o2
1:
	retl
	mov %o2, %o0
#endif

#ifdef L_modsi3
/* This implementation was taken from glibc:
 *
 * Input: dividend and divisor in %o0 and %o1 respectively.
 *
 * Algorithm parameters:
 *  N		how many bits per iteration we try to get (4)
 *  WORDSIZE	total number of bits (32)
 *
 * Derived constants:
 *  TOPBITS	number of bits in the top decade of a number
 *
 * Important variables:
 *  Q		the partial quotient under development (initially 0)
 *  R		the remainder so far, initially the dividend
 *  ITER	number of main division loop iterations required;
 *		equal to ceil(log2(quotient) / N).  Note that this
 *		is the log base (2^N) of the quotient.
 *  V		the current comparand, initially divisor*2^(ITER*N-1)
 *
 * Cost:
 *  Current estimate for non-large dividend is
 *	ceil(log2(quotient) / N) * (10 + 7N/2) + C
 *  A large dividend is one greater than 2^(31-TOPBITS) and takes a
 *  different path, as the upper bits of the quotient must be developed
 *  one bit at a time.
 */
.text
	.align 4
	.global	.urem
	.proc 4
.urem:
	b	divide
	mov	0, %g3		! result always positive

        .align 4
	.global .rem
	.proc 4
.rem:
	! compute sign of result; if neither is negative, no problem
	orcc	%o1, %o0, %g0	! either negative?
	bge	2f			! no, go do the divide
	mov	%o0, %g3		! sign of remainder matches %o0
	tst	%o1
	bge	1f
	tst	%o0
	! %o1 is definitely negative; %o0 might also be negative
	bge	2f			! if %o0 not negative...
	sub	%g0, %o1, %o1	! in any case, make %o1 nonneg
1:	! %o0 is negative, %o1 is nonnegative
	sub	%g0, %o0, %o0	! make %o0 nonnegative
2:

	! Ready to divide.  Compute size of quotient; scale comparand.
divide:
	orcc	%o1, %g0, %o5
	bne	1f
	mov	%o0, %o3

		! Divide by zero trap.  If it returns, return 0 (about as
		! wrong as possible, but that is what SunOS does...).
		ta	0x2   !ST_DIV0
		retl
		clr	%o0

1:
	cmp	%o3, %o5		! if %o1 exceeds %o0, done
	blu	got_result		! (and algorithm fails otherwise)
	clr	%o2
	sethi	%hi(1 << (32 - 4 - 1)), %g1
	cmp	%o3, %g1
	blu	not_really_big
	clr	%o4

	! Here the dividend is >= 2**(31-N) or so.  We must be careful here,
	! as our usual N-at-a-shot divide step will cause overflow and havoc.
	! The number of bits in the result here is N*ITER+SC, where SC <= N.
	! Compute ITER in an unorthodox manner: know we need to shift V into
	! the top decade: so do not even bother to compare to R.
	1:
		cmp	%o5, %g1
		bgeu	3f
		mov	1, %g2
		sll	%o5, 4, %o5
		b	1b
		add	%o4, 1, %o4

	! Now compute %g2.
	2:	addcc	%o5, %o5, %o5
		bcc	not_too_big
		add	%g2, 1, %g2

		! We get here if the %o1 overflowed while shifting.
		! This means that %o3 has the high-order bit set.
		! Restore %o5 and subtract from %o3.
		sll	%g1, 4, %g1	! high order bit
		srl	%o5, 1, %o5		! rest of %o5
		add	%o5, %g1, %o5
		b	do_single_div
		sub	%g2, 1, %g2

	not_too_big:
	3:	cmp	%o5, %o3
		blu	2b
		nop
		be	do_single_div
		nop
	/* NB: these are commented out in the V8-SPARC manual as well */
	/* (I do not understand this) */
	! %o5 > %o3: went too far: back up 1 step
	!	srl	%o5, 1, %o5
	!	dec	%g2
	! do single-bit divide steps
	!
	! We have to be careful here.  We know that %o3 >= %o5, so we can do the
	! first divide step without thinking.  BUT, the others are conditional,
	! and are only done if %o3 >= 0.  Because both %o3 and %o5 may have the high-
	! order bit set in the first step, just falling into the regular
	! division loop will mess up the first time around.
	! So we unroll slightly...
	do_single_div:
		subcc	%g2, 1, %g2
		bl	end_regular_divide
		nop
		sub	%o3, %o5, %o3
		mov	1, %o2
		b	end_single_divloop
		nop
	single_divloop:
		sll	%o2, 1, %o2
		bl	1f
		srl	%o5, 1, %o5
		! %o3 >= 0
		sub	%o3, %o5, %o3
		b	2f
		add	%o2, 1, %o2
	1:	! %o3 < 0
		add	%o3, %o5, %o3
		sub	%o2, 1, %o2
	2:
	end_single_divloop:
		subcc	%g2, 1, %g2
		bge	single_divloop
		tst	%o3
		b,a	end_regular_divide

not_really_big:
1:
	sll	%o5, 4, %o5
	cmp	%o5, %o3
	bleu	1b
	addcc	%o4, 1, %o4
	be	got_result
	sub	%o4, 1, %o4

	tst	%o3	! set up for initial iteration
divloop:
	sll	%o2, 4, %o2
		! depth 1, accumulated bits 0
	bl	L1.16
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 2, accumulated bits 1
	bl	L2.17
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 3, accumulated bits 3
	bl	L3.19
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 4, accumulated bits 7
	bl	L4.23
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (7*2+1), %o2
L4.23:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (7*2-1), %o2
	
L3.19:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 4, accumulated bits 5
	bl	L4.21
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (5*2+1), %o2
	
L4.21:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (5*2-1), %o2
	
L2.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 3, accumulated bits 1
	bl	L3.17
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 4, accumulated bits 3
	bl	L4.19
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (3*2+1), %o2
	
L4.19:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (3*2-1), %o2
	
L3.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 4, accumulated bits 1
	bl	L4.17
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (1*2+1), %o2
	
L4.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (1*2-1), %o2
	
L1.16:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 2, accumulated bits -1
	bl	L2.15
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 3, accumulated bits -1
	bl	L3.15
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 4, accumulated bits -1
	bl	L4.15
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-1*2+1), %o2
	
L4.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-1*2-1), %o2
	
L3.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 4, accumulated bits -3
	bl	L4.13
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-3*2+1), %o2
	
L4.13:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-3*2-1), %o2
	
L2.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 3, accumulated bits -3
	bl	L3.13
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	! depth 4, accumulated bits -5
	bl	L4.11
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-5*2+1), %o2
	
L4.11:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-5*2-1), %o2
	
L3.13:
	! remainder is negative
	addcc	%o3,%o5,%o3
	! depth 4, accumulated bits -7
	bl	L4.9
	srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-7*2+1), %o2
	
L4.9:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	add	%o2, (-7*2-1), %o2
	
	9:
end_regular_divide:
	subcc	%o4, 1, %o4
	bge	divloop
	tst	%o3
	bl,a	got_result
	! non-restoring fixup here (one instruction only!)
	add	%o3, %o1, %o3

got_result:
	! check to see if answer should be < 0
	tst	%g3
	bl,a	1f
	sub %g0, %o3, %o3
1:
	retl
	mov %o3, %o0

#endif

