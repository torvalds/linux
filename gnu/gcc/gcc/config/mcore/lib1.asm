/* libgcc routines for the MCore.
   Copyright (C) 1993, 1999, 2000 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
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

#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a ## b

/* Use the right prefix for global labels.  */

#define SYM(x) CONCAT1 (__, x)

#ifdef __ELF__
#define TYPE(x) .type SYM (x),@function
#define SIZE(x) .size SYM (x), . - SYM (x)
#else
#define TYPE(x)
#define SIZE(x)
#endif

.macro FUNC_START name
	.text
	.globl SYM (\name)
	TYPE (\name)
SYM (\name):
.endm

.macro FUNC_END name
	SIZE (\name)
.endm

#ifdef	L_udivsi3
FUNC_START udiv32
FUNC_START udivsi32

	movi	r1,0		// r1-r2 form 64 bit dividend
	movi	r4,1		// r4 is quotient (1 for a sentinel)

	cmpnei	r3,0		// look for 0 divisor
	bt	9f
	trap	3		// divide by 0
9:
	// control iterations; skip across high order 0 bits in dividend
	mov	r7,r2
	cmpnei	r7,0
	bt	8f
	movi	r2,0		// 0 dividend
	jmp	r15		// quick return
8:
	ff1	r7		// figure distance to skip
	lsl	r4,r7		// move the sentinel along (with 0's behind)
	lsl	r2,r7		// and the low 32 bits of numerator

// appears to be wrong...
// tested out incorrectly in our OS work...
//	mov	r7,r3		// looking at divisor
//	ff1	r7		// I can move 32-r7 more bits to left.
//	addi	r7,1		// ok, one short of that...
//	mov	r1,r2
//	lsr	r1,r7		// bits that came from low order...
//	rsubi	r7,31		// r7 == "32-n" == LEFT distance
//	addi	r7,1		// this is (32-n)
//	lsl	r4,r7		// fixes the high 32 (quotient)
//	lsl	r2,r7
//	cmpnei	r4,0
//	bf	4f		// the sentinel went away...

	// run the remaining bits

1:	lslc	r2,1		// 1 bit left shift of r1-r2
	addc	r1,r1
	cmphs	r1,r3		// upper 32 of dividend >= divisor?
	bf	2f
	sub	r1,r3		// if yes, subtract divisor
2:	addc	r4,r4		// shift by 1 and count subtracts
	bf	1b		// if sentinel falls out of quotient, stop

4:	mov	r2,r4		// return quotient
	mov	r3,r1		// and piggyback the remainder
	jmp	r15
FUNC_END udiv32
FUNC_END udivsi32
#endif

#ifdef	L_umodsi3
FUNC_START urem32
FUNC_START umodsi3
	movi	r1,0		// r1-r2 form 64 bit dividend
	movi	r4,1		// r4 is quotient (1 for a sentinel)
	cmpnei	r3,0		// look for 0 divisor
	bt	9f
	trap	3		// divide by 0
9:
	// control iterations; skip across high order 0 bits in dividend
	mov	r7,r2
	cmpnei	r7,0
	bt	8f
	movi	r2,0		// 0 dividend
	jmp	r15		// quick return
8:
	ff1	r7		// figure distance to skip
	lsl	r4,r7		// move the sentinel along (with 0's behind)
	lsl	r2,r7		// and the low 32 bits of numerator

1:	lslc	r2,1		// 1 bit left shift of r1-r2
	addc	r1,r1
	cmphs	r1,r3		// upper 32 of dividend >= divisor?
	bf	2f
	sub	r1,r3		// if yes, subtract divisor
2:	addc	r4,r4		// shift by 1 and count subtracts
	bf	1b		// if sentinel falls out of quotient, stop
	mov	r2,r1		// return remainder
	jmp	r15
FUNC_END urem32
FUNC_END umodsi3
#endif

#ifdef	L_divsi3
FUNC_START div32
FUNC_START divsi3
	mov	r5,r2		// calc sign of quotient
	xor	r5,r3
	abs	r2		// do unsigned divide
	abs	r3
	movi	r1,0		// r1-r2 form 64 bit dividend
	movi	r4,1		// r4 is quotient (1 for a sentinel)
	cmpnei	r3,0		// look for 0 divisor
	bt	9f
	trap	3		// divide by 0
9:
	// control iterations; skip across high order 0 bits in dividend
	mov	r7,r2
	cmpnei	r7,0
	bt	8f
	movi	r2,0		// 0 dividend
	jmp	r15		// quick return
8:
	ff1	r7		// figure distance to skip
	lsl	r4,r7		// move the sentinel along (with 0's behind)
	lsl	r2,r7		// and the low 32 bits of numerator

// tested out incorrectly in our OS work...
//	mov	r7,r3		// looking at divisor
//	ff1	r7		// I can move 32-r7 more bits to left.
//	addi	r7,1		// ok, one short of that...
//	mov	r1,r2
//	lsr	r1,r7		// bits that came from low order...
//	rsubi	r7,31		// r7 == "32-n" == LEFT distance
//	addi	r7,1		// this is (32-n)
//	lsl	r4,r7		// fixes the high 32 (quotient)
//	lsl	r2,r7
//	cmpnei	r4,0
//	bf	4f		// the sentinel went away...

	// run the remaining bits
1:	lslc	r2,1		// 1 bit left shift of r1-r2
	addc	r1,r1
	cmphs	r1,r3		// upper 32 of dividend >= divisor?
	bf	2f
	sub	r1,r3		// if yes, subtract divisor
2:	addc	r4,r4		// shift by 1 and count subtracts
	bf	1b		// if sentinel falls out of quotient, stop

4:	mov	r2,r4		// return quotient
	mov	r3,r1		// piggyback the remainder
	btsti	r5,31		// after adjusting for sign
	bf	3f
	rsubi	r2,0
	rsubi	r3,0
3:	jmp	r15
FUNC_END div32
FUNC_END divsi3
#endif

#ifdef	L_modsi3
FUNC_START rem32
FUNC_START modsi3
	mov	r5,r2		// calc sign of remainder
	abs	r2		// do unsigned divide
	abs	r3
	movi	r1,0		// r1-r2 form 64 bit dividend
	movi	r4,1		// r4 is quotient (1 for a sentinel)
	cmpnei	r3,0		// look for 0 divisor
	bt	9f
	trap	3		// divide by 0
9: 
	// control iterations; skip across high order 0 bits in dividend
	mov	r7,r2
	cmpnei	r7,0
	bt	8f
	movi	r2,0		// 0 dividend
	jmp	r15		// quick return
8:
	ff1	r7		// figure distance to skip
	lsl	r4,r7		// move the sentinel along (with 0's behind)
	lsl	r2,r7		// and the low 32 bits of numerator

1:	lslc	r2,1		// 1 bit left shift of r1-r2
	addc	r1,r1
	cmphs	r1,r3		// upper 32 of dividend >= divisor?
	bf	2f
	sub	r1,r3		// if yes, subtract divisor
2:	addc	r4,r4		// shift by 1 and count subtracts
	bf	1b		// if sentinel falls out of quotient, stop
	mov	r2,r1		// return remainder
	btsti	r5,31		// after adjusting for sign
	bf	3f
	rsubi	r2,0
3:	jmp	r15
FUNC_END rem32
FUNC_END modsi3
#endif


/* GCC expects that {__eq,__ne,__gt,__ge,__le,__lt}{df2,sf2}
   will behave as __cmpdf2. So, we stub the implementations to
   jump on to __cmpdf2 and __cmpsf2.
 
   All of these shortcircuit the return path so that __cmp{sd}f2
   will go directly back to the caller.  */

.macro  COMPARE_DF_JUMP name
	.import SYM (cmpdf2)
FUNC_START \name
	jmpi SYM (cmpdf2)
FUNC_END \name
.endm
		
#ifdef  L_eqdf2
COMPARE_DF_JUMP eqdf2
#endif /* L_eqdf2 */

#ifdef  L_nedf2
COMPARE_DF_JUMP nedf2
#endif /* L_nedf2 */

#ifdef  L_gtdf2
COMPARE_DF_JUMP gtdf2
#endif /* L_gtdf2 */

#ifdef  L_gedf2
COMPARE_DF_JUMP gedf2
#endif /* L_gedf2 */

#ifdef  L_ltdf2
COMPARE_DF_JUMP ltdf2
#endif /* L_ltdf2 */
	
#ifdef  L_ledf2
COMPARE_DF_JUMP ledf2
#endif /* L_ledf2 */

/* SINGLE PRECISION FLOATING POINT STUBS */

.macro  COMPARE_SF_JUMP name
	.import SYM (cmpsf2)
FUNC_START \name
	jmpi SYM (cmpsf2)
FUNC_END \name
.endm
		
#ifdef  L_eqsf2
COMPARE_SF_JUMP eqsf2
#endif /* L_eqsf2 */
	
#ifdef  L_nesf2
COMPARE_SF_JUMP nesf2
#endif /* L_nesf2 */
	
#ifdef  L_gtsf2
COMPARE_SF_JUMP gtsf2
#endif /* L_gtsf2 */
	
#ifdef  L_gesf2
COMPARE_SF_JUMP __gesf2
#endif /* L_gesf2 */
	
#ifdef  L_ltsf2
COMPARE_SF_JUMP __ltsf2
#endif /* L_ltsf2 */
	
#ifdef  L_lesf2
COMPARE_SF_JUMP lesf2
#endif /* L_lesf2 */
