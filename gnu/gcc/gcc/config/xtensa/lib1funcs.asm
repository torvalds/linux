/* Assembly functions for the Xtensa version of libgcc1.
   Copyright (C) 2001, 2002, 2003, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Bob Wilson (bwilson@tensilica.com) at Tensilica.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "xtensa-config.h"

# Define macros for the ABS and ADDX* instructions to handle cases
# where they are not included in the Xtensa processor configuration.

	.macro	do_abs dst, src, tmp
#if XCHAL_HAVE_ABS
	abs	\dst, \src
#else
	neg	\tmp, \src
	movgez	\tmp, \src, \src
	mov	\dst, \tmp
#endif
	.endm

	.macro	do_addx2 dst, as, at, tmp
#if XCHAL_HAVE_ADDX
	addx2	\dst, \as, \at
#else
	slli	\tmp, \as, 1
	add	\dst, \tmp, \at
#endif
	.endm

	.macro	do_addx4 dst, as, at, tmp
#if XCHAL_HAVE_ADDX
	addx4	\dst, \as, \at
#else
	slli	\tmp, \as, 2
	add	\dst, \tmp, \at
#endif
	.endm

	.macro	do_addx8 dst, as, at, tmp
#if XCHAL_HAVE_ADDX
	addx8	\dst, \as, \at
#else
	slli	\tmp, \as, 3
	add	\dst, \tmp, \at
#endif
	.endm

# Define macros for leaf function entry and return, supporting either the
# standard register windowed ABI or the non-windowed call0 ABI.  These
# macros do not allocate any extra stack space, so they only work for
# leaf functions that do not need to spill anything to the stack.

	.macro leaf_entry reg, size
#if XCHAL_HAVE_WINDOWED && !__XTENSA_CALL0_ABI__
	entry \reg, \size
#else
	/* do nothing */
#endif
	.endm

	.macro leaf_return
#if XCHAL_HAVE_WINDOWED && !__XTENSA_CALL0_ABI__
	retw
#else
	ret
#endif
	.endm


#ifdef L_mulsi3
	.align	4
	.global	__mulsi3
	.type	__mulsi3,@function
__mulsi3:
	leaf_entry sp, 16

#if XCHAL_HAVE_MUL16
	or	a4, a2, a3
	srai	a4, a4, 16
	bnez	a4, .LMUL16
	mul16u	a2, a2, a3
	leaf_return
.LMUL16:
	srai	a4, a2, 16
	srai	a5, a3, 16
	mul16u	a7, a4, a3
	mul16u	a6, a5, a2
	mul16u	a4, a2, a3
	add	a7, a7, a6
	slli	a7, a7, 16
	add	a2, a7, a4

#elif XCHAL_HAVE_MAC16
	mul.aa.hl a2, a3
	mula.aa.lh a2, a3
	rsr	a5, ACCLO
	umul.aa.ll a2, a3
	rsr	a4, ACCLO
	slli	a5, a5, 16
	add	a2, a4, a5

#else /* !XCHAL_HAVE_MUL16 && !XCHAL_HAVE_MAC16 */

	# Multiply one bit at a time, but unroll the loop 4x to better
	# exploit the addx instructions and avoid overhead.
	# Peel the first iteration to save a cycle on init.

	# Avoid negative numbers.
	xor	a5, a2, a3  # top bit is 1 iff one of the inputs is negative
	do_abs	a3, a3, a6
	do_abs	a2, a2, a6

	# Swap so the second argument is smaller.
	sub	a7, a2, a3
	mov	a4, a3
	movgez	a4, a2, a7  # a4 = max(a2, a3) 
	movltz	a3, a2, a7  # a3 = min(a2, a3)

	movi	a2, 0
	extui	a6, a3, 0, 1
	movnez	a2, a4, a6

	do_addx2 a7, a4, a2, a7
	extui	a6, a3, 1, 1
	movnez	a2, a7, a6

	do_addx4 a7, a4, a2, a7
	extui	a6, a3, 2, 1
	movnez	a2, a7, a6

	do_addx8 a7, a4, a2, a7
	extui	a6, a3, 3, 1
	movnez	a2, a7, a6

	bgeui	a3, 16, .Lmult_main_loop
	neg	a3, a2
	movltz	a2, a3, a5
	leaf_return

	.align	4
.Lmult_main_loop:
	srli	a3, a3, 4
	slli	a4, a4, 4

	add	a7, a4, a2
	extui	a6, a3, 0, 1
	movnez	a2, a7, a6

	do_addx2 a7, a4, a2, a7
	extui	a6, a3, 1, 1
	movnez	a2, a7, a6

	do_addx4 a7, a4, a2, a7
	extui	a6, a3, 2, 1
	movnez	a2, a7, a6

	do_addx8 a7, a4, a2, a7
	extui	a6, a3, 3, 1
	movnez	a2, a7, a6

	bgeui	a3, 16, .Lmult_main_loop

	neg	a3, a2
	movltz	a2, a3, a5

#endif /* !XCHAL_HAVE_MUL16 && !XCHAL_HAVE_MAC16 */

	leaf_return
	.size	__mulsi3,.-__mulsi3

#endif /* L_mulsi3 */


# Define a macro for the NSAU (unsigned normalize shift amount)
# instruction, which computes the number of leading zero bits,
# to handle cases where it is not included in the Xtensa processor
# configuration.

	.macro	do_nsau cnt, val, tmp, a
#if XCHAL_HAVE_NSA
	nsau	\cnt, \val
#else
	mov	\a, \val
	movi	\cnt, 0
	extui	\tmp, \a, 16, 16
	bnez	\tmp, 0f
	movi	\cnt, 16
	slli	\a, \a, 16
0:	
	extui	\tmp, \a, 24, 8
	bnez	\tmp, 1f
	addi	\cnt, \cnt, 8
	slli	\a, \a, 8
1:	
	movi	\tmp, __nsau_data
	extui	\a, \a, 24, 8
	add	\tmp, \tmp, \a
	l8ui	\tmp, \tmp, 0
	add	\cnt, \cnt, \tmp
#endif /* !XCHAL_HAVE_NSA */
	.endm

#ifdef L_nsau
	.section .rodata
	.align	4
	.global	__nsau_data
	.type	__nsau_data,@object
__nsau_data:	
#if !XCHAL_HAVE_NSA
	.byte	8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4
	.byte	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
	.byte	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
	.byte	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
	.byte	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
	.byte	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
	.byte	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
	.byte	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
#endif /* !XCHAL_HAVE_NSA */
	.size	__nsau_data,.-__nsau_data
	.hidden	__nsau_data
#endif /* L_nsau */


#ifdef L_udivsi3
	.align	4
	.global	__udivsi3
	.type	__udivsi3,@function
__udivsi3:
	leaf_entry sp, 16
	bltui	a3, 2, .Lle_one	# check if the divisor <= 1

	mov	a6, a2		# keep dividend in a6
	do_nsau	a5, a6, a2, a7	# dividend_shift = nsau(dividend)
	do_nsau	a4, a3, a2, a7	# divisor_shift = nsau(divisor)
	bgeu	a5, a4, .Lspecial

	sub	a4, a4, a5	# count = divisor_shift - dividend_shift
	ssl	a4
	sll	a3, a3		# divisor <<= count
	movi	a2, 0		# quotient = 0

	# test-subtract-and-shift loop; one quotient bit on each iteration
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lloopend
#endif /* XCHAL_HAVE_LOOPS */
.Lloop:
	bltu	a6, a3, .Lzerobit
	sub	a6, a6, a3
	addi	a2, a2, 1
.Lzerobit:
	slli	a2, a2, 1
	srli	a3, a3, 1
#if !XCHAL_HAVE_LOOPS
	addi	a4, a4, -1
	bnez	a4, .Lloop
#endif /* !XCHAL_HAVE_LOOPS */
.Lloopend:

	bltu	a6, a3, .Lreturn
	addi	a2, a2, 1	# increment quotient if dividend >= divisor
.Lreturn:
	leaf_return

.Lle_one:
	beqz	a3, .Lerror	# if divisor == 1, return the dividend
	leaf_return

.Lspecial:
	# return dividend >= divisor
	bltu	a6, a3, .Lreturn0
	movi	a2, 1
	leaf_return

.Lerror:
	# just return 0; could throw an exception

.Lreturn0:
	movi	a2, 0
	leaf_return
	.size	__udivsi3,.-__udivsi3

#endif /* L_udivsi3 */


#ifdef L_divsi3
	.align	4
	.global	__divsi3
	.type	__divsi3,@function
__divsi3:
	leaf_entry sp, 16
	xor	a7, a2, a3	# sign = dividend ^ divisor
	do_abs	a6, a2, a4	# udividend = abs(dividend)
	do_abs	a3, a3, a4	# udivisor = abs(divisor)
	bltui	a3, 2, .Lle_one	# check if udivisor <= 1
	do_nsau	a5, a6, a2, a8	# udividend_shift = nsau(udividend)
	do_nsau	a4, a3, a2, a8	# udivisor_shift = nsau(udivisor)
	bgeu	a5, a4, .Lspecial

	sub	a4, a4, a5	# count = udivisor_shift - udividend_shift
	ssl	a4
	sll	a3, a3		# udivisor <<= count
	movi	a2, 0		# quotient = 0

	# test-subtract-and-shift loop; one quotient bit on each iteration
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lloopend
#endif /* XCHAL_HAVE_LOOPS */
.Lloop:
	bltu	a6, a3, .Lzerobit
	sub	a6, a6, a3
	addi	a2, a2, 1
.Lzerobit:
	slli	a2, a2, 1
	srli	a3, a3, 1
#if !XCHAL_HAVE_LOOPS
	addi	a4, a4, -1
	bnez	a4, .Lloop
#endif /* !XCHAL_HAVE_LOOPS */
.Lloopend:

	bltu	a6, a3, .Lreturn
	addi	a2, a2, 1	# increment quotient if udividend >= udivisor
.Lreturn:
	neg	a5, a2
	movltz	a2, a5, a7	# return (sign < 0) ? -quotient : quotient
	leaf_return

.Lle_one:
	beqz	a3, .Lerror
	neg	a2, a6		# if udivisor == 1, then return...
	movgez	a2, a6, a7	# (sign < 0) ? -udividend : udividend
	leaf_return

.Lspecial:
	bltu	a6, a3, .Lreturn0 #  if dividend < divisor, return 0
	movi	a2, 1
	movi	a4, -1
	movltz	a2, a4, a7	# else return (sign < 0) ? -1 :	 1 
	leaf_return

.Lerror:
	# just return 0; could throw an exception

.Lreturn0:
	movi	a2, 0
	leaf_return
	.size	__divsi3,.-__divsi3

#endif /* L_divsi3 */


#ifdef L_umodsi3
	.align	4
	.global	__umodsi3
	.type	__umodsi3,@function
__umodsi3:
	leaf_entry sp, 16
	bltui	a3, 2, .Lle_one	# check if the divisor is <= 1

	do_nsau	a5, a2, a6, a7	# dividend_shift = nsau(dividend)
	do_nsau	a4, a3, a6, a7	# divisor_shift = nsau(divisor)
	bgeu	a5, a4, .Lspecial

	sub	a4, a4, a5	# count = divisor_shift - dividend_shift
	ssl	a4
	sll	a3, a3		# divisor <<= count

	# test-subtract-and-shift loop
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lloopend
#endif /* XCHAL_HAVE_LOOPS */
.Lloop:
	bltu	a2, a3, .Lzerobit
	sub	a2, a2, a3
.Lzerobit:
	srli	a3, a3, 1
#if !XCHAL_HAVE_LOOPS
	addi	a4, a4, -1
	bnez	a4, .Lloop
#endif /* !XCHAL_HAVE_LOOPS */
.Lloopend:

.Lspecial:
	bltu	a2, a3, .Lreturn
	sub	a2, a2, a3	# subtract once more if dividend >= divisor
.Lreturn:
	leaf_return

.Lle_one:
	# the divisor is either 0 or 1, so just return 0.
	# someday we may want to throw an exception if the divisor is 0.
	movi	a2, 0
	leaf_return
	.size	__umodsi3,.-__umodsi3

#endif /* L_umodsi3 */


#ifdef L_modsi3
	.align	4
	.global	__modsi3
	.type	__modsi3,@function
__modsi3:
	leaf_entry sp, 16
	mov	a7, a2		# save original (signed) dividend
	do_abs	a2, a2, a4	# udividend = abs(dividend)
	do_abs	a3, a3, a4	# udivisor = abs(divisor)
	bltui	a3, 2, .Lle_one	# check if udivisor <= 1
	do_nsau	a5, a2, a6, a8	# udividend_shift = nsau(udividend)
	do_nsau	a4, a3, a6, a8	# udivisor_shift = nsau(udivisor)
	bgeu	a5, a4, .Lspecial

	sub	a4, a4, a5	# count = udivisor_shift - udividend_shift
	ssl	a4
	sll	a3, a3		# udivisor <<= count

	# test-subtract-and-shift loop
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lloopend
#endif /* XCHAL_HAVE_LOOPS */
.Lloop:
	bltu	a2, a3, .Lzerobit
	sub	a2, a2, a3
.Lzerobit:
	srli	a3, a3, 1
#if !XCHAL_HAVE_LOOPS
	addi	a4, a4, -1
	bnez	a4, .Lloop
#endif /* !XCHAL_HAVE_LOOPS */
.Lloopend:

.Lspecial:
	bltu	a2, a3, .Lreturn
	sub	a2, a2, a3	# subtract once more if udividend >= udivisor
.Lreturn:
	bgez	a7, .Lpositive
	neg	a2, a2		# if (dividend < 0), return -udividend
.Lpositive:	
	leaf_return

.Lle_one:
	# udivisor is either 0 or 1, so just return 0.
	# someday we may want to throw an exception if udivisor is 0.
	movi	a2, 0
	leaf_return
	.size	__modsi3,.-__modsi3

#endif /* L_modsi3 */

#include "ieee754-df.S"
#include "ieee754-sf.S"
