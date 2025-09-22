/* DEC Alpha division and remainder support.
   Copyright (C) 1994, 1999 Free Software Foundation, Inc.

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

/* This had to be written in assembler because the division functions 
   use a non-standard calling convention. 

   This file provides an implementation of __divqu, __divq, __divlu, 
   __divl, __remqu, __remq, __remlu and __reml.  CPP macros control
   the exact operation.

   Operation performed: $27 := $24 o $25, clobber $28, return address to
   caller in $23, where o one of the operations.

   The following macros need to be defined: 

	SIZE, the number of bits, 32 or 64.

	TYPE, either UNSIGNED or SIGNED

	OPERATION, either DIVISION or REMAINDER
   
	SPECIAL_CALLING_CONVENTION, 0 or 1.  It is useful for debugging to
	define this to 0.  That removes the `__' prefix to make the function
	name not collide with the existing libc.a names, and uses the
	standard Alpha procedure calling convention.
*/

#ifndef SPECIAL_CALLING_CONVENTION
#define SPECIAL_CALLING_CONVENTION 1
#endif

#ifdef L_divl
#if SPECIAL_CALLING_CONVENTION
#define FUNCTION_NAME __divl
#else
#define FUNCTION_NAME divl
#endif
#define SIZE 32
#define TYPE SIGNED
#define OPERATION DIVISION
#endif

#ifdef L_divlu
#if SPECIAL_CALLING_CONVENTION
#define FUNCTION_NAME __divlu
#else
#define FUNCTION_NAME divlu
#endif
#define SIZE 32
#define TYPE UNSIGNED
#define OPERATION DIVISION
#endif

#ifdef L_divq
#if SPECIAL_CALLING_CONVENTION
#define FUNCTION_NAME __divq
#else
#define FUNCTION_NAME divq
#endif
#define SIZE 64
#define TYPE SIGNED
#define OPERATION DIVISION
#endif

#ifdef L_divqu
#if SPECIAL_CALLING_CONVENTION
#define FUNCTION_NAME __divqu
#else
#define FUNCTION_NAME divqu
#endif
#define SIZE 64
#define TYPE UNSIGNED
#define OPERATION DIVISION
#endif

#ifdef L_reml
#if SPECIAL_CALLING_CONVENTION
#define FUNCTION_NAME __reml
#else
#define FUNCTION_NAME reml
#endif
#define SIZE 32
#define TYPE SIGNED
#define OPERATION REMAINDER
#endif

#ifdef L_remlu
#if SPECIAL_CALLING_CONVENTION
#define FUNCTION_NAME __remlu
#else
#define FUNCTION_NAME remlu
#endif
#define SIZE 32
#define TYPE UNSIGNED
#define OPERATION REMAINDER
#endif

#ifdef L_remq
#if SPECIAL_CALLING_CONVENTION
#define FUNCTION_NAME __remq
#else
#define FUNCTION_NAME remq
#endif
#define SIZE 64
#define TYPE SIGNED
#define OPERATION REMAINDER
#endif

#ifdef L_remqu
#if SPECIAL_CALLING_CONVENTION
#define FUNCTION_NAME __remqu
#else
#define FUNCTION_NAME remqu
#endif
#define SIZE 64
#define TYPE UNSIGNED
#define OPERATION REMAINDER
#endif

#define tmp0 $3
#define tmp1 $28
#define cnt $1
#define result_sign $2

#if SPECIAL_CALLING_CONVENTION
#define N $24
#define D $25
#define Q RETREG
#define RETREG $27
#else
#define N $16
#define D $17
#define Q RETREG
#define RETREG $0
#endif

/* Misc symbols to make alpha assembler easier to read.  */
#define zero $31
#define sp $30

/* Symbols to make interface nicer.  */
#define UNSIGNED 0
#define SIGNED 1
#define DIVISION 0
#define REMAINDER 1

	.set noreorder
	.set noat
.text
	.align 3
	.globl FUNCTION_NAME
	.ent FUNCTION_NAME
FUNCTION_NAME:

	.frame	$30,0,$26,0
	.prologue 0

/* Under the special calling convention, we have to preserve all register
   values but $23 and $28.  */
#if SPECIAL_CALLING_CONVENTION
	lda	sp,-64(sp)
#if OPERATION == DIVISION
	stq	N,0(sp)
#endif
	stq	D,8(sp)
	stq	cnt,16(sp)
	stq	result_sign,24(sp)
	stq	tmp0,32(sp)
#endif

/* If we are computing the remainder, move N to the register that is used
   for the return value, and redefine what register is used for N.  */
#if OPERATION == REMAINDER
	bis	N,N,RETREG
#undef N
#define N RETREG
#endif

/* Perform conversion from 32 bit types to 64 bit types.  */
#if SIZE == 32
#if TYPE == SIGNED
	/* If there are problems with the signed case, add these instructions.
	   The caller should already have done this.
	addl	N,0,N		# sign extend N
	addl	D,0,D		# sign extend D
	*/
#else /* UNSIGNED */
	zap	N,0xf0,N	# zero extend N (caller required to sign extend)
	zap	D,0xf0,D	# zero extend D
#endif
#endif

/* Check for divide by zero.  */
	bne	D,$34
	lda	$16,-2(zero)
	call_pal 0xaa
$34:

#if TYPE == SIGNED
#if OPERATION == DIVISION
	xor	N,D,result_sign
#else
	bis	N,N,result_sign
#endif
/* Get the absolute values of N and D.  */
	subq	zero,N,tmp0
	cmovlt	N,tmp0,N
	subq	zero,D,tmp0
	cmovlt	D,tmp0,D
#endif

/* Compute CNT = ceil(log2(N)) - ceil(log2(D)).  This is the number of
   divide iterations we will have to perform.  Should you wish to optimize
   this, check a few bits at a time, preferably using zap/zapnot.  Be
   careful though, this code runs fast fro the most common cases, when the
   quotient is small.  */
	bge	N,$35
	bis	zero,1,cnt
	blt	D,$40
	.align	3
$39:	addq	D,D,D
	addl	cnt,1,cnt
	bge	D,$39
	br	zero,$40
$35:	cmpult	N,D,tmp0
	bis	zero,zero,cnt
	bne	tmp0,$42
	.align	3
$44:	addq	D,D,D
	cmpult	N,D,tmp0
	addl	cnt,1,cnt
	beq	tmp0,$44
$42:	srl	D,1,D
$40:
	subl	cnt,1,cnt


/* Actual divide.  Could be optimized with unrolling.  */
#if OPERATION == DIVISION
	bis	zero,zero,Q
#endif
	blt	cnt,$46
	.align	3
$49:	cmpule	D,N,tmp1
	subq	N,D,tmp0
	srl	D,1,D
	subl	cnt,1,cnt
	cmovne	tmp1,tmp0,N
#if OPERATION == DIVISION
	addq	Q,Q,Q
	bis	Q,tmp1,Q
#endif
	bge	cnt,$49
$46:


/* The result is now in RETREG.  NOTE!  It was written to RETREG using
   either N or Q as a synonym!  */


/* Change the sign of the result as needed.  */
#if TYPE == SIGNED
	subq	zero,RETREG,tmp0
	cmovlt	result_sign,tmp0,RETREG
#endif


/* Restore clobbered registers.  */
#if SPECIAL_CALLING_CONVENTION
#if OPERATION == DIVISION
	ldq	N,0(sp)
#endif
	ldq	D,8(sp)
	ldq	cnt,16(sp)
	ldq	result_sign,24(sp)
	ldq	tmp0,32(sp)

	lda	sp,64(sp)
#endif


/* Sign extend an *unsigned* 32 bit result, as required by the Alpha
   conventions.  */
#if TYPE == UNSIGNED && SIZE == 32
	/* This could be avoided by adding some CPP hair to the divide loop.
	   It is probably not worth the added complexity.  */
	addl	RETREG,0,RETREG
#endif


#if SPECIAL_CALLING_CONVENTION
	ret	zero,($23),1
#else
	ret	zero,($26),1
#endif
	.end	FUNCTION_NAME
