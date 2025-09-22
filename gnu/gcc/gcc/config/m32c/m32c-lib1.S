/* libgcc routines for R8C/M16C/M32C
   Copyright (C) 2005
   Free Software Foundation, Inc.
   Contributed by Red Hat.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

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

#if defined(__r8c_cpu__) || defined(__m16c_cpu__)
#define A16
#define A(n,w) n
#define W w
#else
#define A24
#define A(n,w) w
#define W l
#endif


#ifdef L__m32c_memregs

/* Warning: these memory locations are used as a register bank.  They
   *must* end up consecutive in any final executable, so you may *not*
   use the otherwise obvious ".comm" directive to allocate space for
   them. */

	.bss
	.global	mem0
mem0:	.space	1
	.global	mem1
mem1:	.space	1
	.global	mem2
mem2:	.space	1
	.global	mem3
mem3:	.space	1
	.global	mem4
mem4:	.space	1
	.global	mem5
mem5:	.space	1
	.global	mem6
mem6:	.space	1
	.global	mem7
mem7:	.space	1
	.global	mem8
mem8:	.space	1
	.global	mem9
mem9:	.space	1
	.global	mem10
mem10:	.space	1
	.global	mem11
mem11:	.space	1
	.global	mem12
mem12:	.space	1
	.global	mem13
mem13:	.space	1
	.global	mem14
mem14:	.space	1
	.global	mem15
mem15:	.space	1

#endif

#ifdef L__m32c_eh_return
	.text
	.global __m32c_eh_return
__m32c_eh_return:	

	/* At this point, r0 has the stack adjustment, r1r3 has the
	   address to return to.  The stack looks like this:

	   old_ra
	   old_fp
	   <- unwound sp
	   ...
	   fb
	   through
	   r0
	   <- sp

	   What we need to do is restore all the registers, update the
	   stack, and return to the right place.
	*/

	stc	sp,a0
	
	add.W	A(#16,#24),a0
	/* a0 points to the current stack, just above the register
	   save areas */

	mov.w	a0,a1
	exts.w	r0
	sub.W	A(r0,r2r0),a1
	sub.W	A(#3,#4),a1
	/* a1 points to the new stack.  */

	/* This is for the "rts" below.  */
	mov.w	r1,[a1]
#ifdef A16
	mov.w	r2,r1
	mov.b	r1l,2[a1]
#else
	mov.w	r2,2[a1]
#endif

	/* This is for the "popc sp" below.  */
	mov.W	a1,[a0]	

	popm    r0,r1,r2,r3,a0,a1,sb,fb
	popc	sp
	rts
#endif

/* SImode arguments for SI foo(SI,SI) functions.  */
#ifdef A16
#define SAL  5[fb]
#define SAH  7[fb]
#define SBL  9[fb]
#define SBH 11[fb]
#else
#define SAL  8[fb]
#define SAH 10[fb]
#define SBL 12[fb]
#define SBH 14[fb]
#endif

#ifdef L__m32c_mulsi3
	.text
	.global ___mulsi3
___mulsi3:
	enter	#0
	push.w	r2
	mov.w	SAL,r0
	mulu.w	SBL,r0		/* writes to r2r0 */
	mov.w	r0,mem0
	mov.w	r2,mem2
	mov.w	SAL,r0
	mulu.w	SBH,r0		/* writes to r2r0 */
	add.w	r0,mem2
	mov.w	SAH,r0
	mulu.w	SBL,r0		/* writes to r2r0 */
	add.w	r0,mem2
	pop.w	r2
	exitd
#endif

#ifdef L__m32c_cmpsi2
	.text
	.global ___cmpsi2
___cmpsi2:
	enter	#0
	cmp.w	SBH,SAH
	jgt	cmpsi_gt
	jlt	cmpsi_lt
	cmp.w	SBL,SAL
	jgt	cmpsi_gt
	jlt	cmpsi_lt
	mov.w	#1,r0
	exitd
cmpsi_gt:
	mov.w	#2,r0
	exitd
cmpsi_lt:
	mov.w	#0,r0
	exitd
#endif

#ifdef L__m32c_ucmpsi2
	.text
	.global ___ucmpsi2
___ucmpsi2:
	enter	#0
	cmp.w	SBH,SAH
	jgtu	cmpsi_gt
	jltu	cmpsi_lt
	cmp.w	SBL,SAL
	jgtu	cmpsi_gt
	jltu	cmpsi_lt
	mov.w	#1,r0
	exitd
cmpsi_gt:
	mov.w	#2,r0
	exitd
cmpsi_lt:
	mov.w	#0,r0
	exitd
#endif

#ifdef L__m32c_jsri16
	.data
m32c_jsri_addr:
	.byte	0, 0, 0
m32c_jsri_ret:
	.byte	0, 0, 0

	.text
	.global	m32c_jsri16
m32c_jsri16:
	pop.w	m32c_jsri_ret
	pop.b	m32c_jsri_ret+2
	pop.w	m32c_jsri_addr
	push.b	m32c_jsri_ret+2
	push.w	m32c_jsri_ret
	jmpi.a	m32c_jsri_addr

#endif
