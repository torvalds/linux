/* Copyright (C) 2006 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

!! Linux specific atomic routines for the Renesas / SuperH SH CPUs.
!! Linux kernel for SH3/4 has implemented the support for software
!! atomic sequences.

#define FUNC(X)		.type X,@function
#define HIDDEN_FUNC(X)	FUNC(X); .hidden X
#define ENDFUNC0(X)	.Lfe_##X: .size X,.Lfe_##X-X
#define ENDFUNC(X)	ENDFUNC0(X)

#if ! __SH5__

#define ATOMIC_TEST_AND_SET(N,T) \
	.global	__sync_lock_test_and_set_##N; \
	HIDDEN_FUNC(__sync_lock_test_and_set_##N); \
	.align	2; \
__sync_lock_test_and_set_##N:; \
	mova	1f, r0; \
	nop; \
	mov	r15, r1; \
	mov	#(0f-1f), r15; \
0:	mov.##T	@r4, r2; \
	mov.##T	r5, @r4; \
1:	mov	r1, r15; \
	rts; \
	 mov	r2, r0; \
	ENDFUNC(__sync_lock_test_and_set_##N)

ATOMIC_TEST_AND_SET (1,b)
ATOMIC_TEST_AND_SET (2,w)
ATOMIC_TEST_AND_SET (4,l)

#define ATOMIC_COMPARE_AND_SWAP(N,T) \
	.global	__sync_compare_and_swap_##N; \
	HIDDEN_FUNC(__sync_compare_and_swap_##N); \
	.align	2; \
__sync_compare_and_swap_##N:; \
	mova	1f, r0; \
	nop; \
	mov	r15, r1; \
	mov	#(0f-1f), r15; \
0:	mov.##T	@r4, r2; \
	cmp/eq	r2, r5; \
	bf	1f; \
	mov.##T	r6, @r4; \
1:	mov	r1, r15; \
	rts; \
	 mov	r2, r0; \
	ENDFUNC(__sync_compare_and_swap_##N)

ATOMIC_COMPARE_AND_SWAP (1,b)
ATOMIC_COMPARE_AND_SWAP (2,w)
ATOMIC_COMPARE_AND_SWAP (4,l)

#define ATOMIC_FETCH_AND_OP(OP,N,T) \
	.global	__sync_fetch_and_##OP##_##N; \
	HIDDEN_FUNC(__sync_fetch_and_##OP##_##N); \
	.align	2; \
__sync_fetch_and_##OP##_##N:; \
	mova	1f, r0; \
	mov	r15, r1; \
	mov	#(0f-1f), r15; \
0:	mov.##T	@r4, r2; \
	OP	r2, r5; \
	mov.##T	r5, @r4; \
1:	mov	r1, r15; \
	rts; \
	 mov	r2, r0; \
	ENDFUNC(__sync_fetch_and_##OP##_##N)

ATOMIC_FETCH_AND_OP(add,1,b)
ATOMIC_FETCH_AND_OP(add,2,w)
ATOMIC_FETCH_AND_OP(add,4,l)

ATOMIC_FETCH_AND_OP(or,1,b)
ATOMIC_FETCH_AND_OP(or,2,w)
ATOMIC_FETCH_AND_OP(or,4,l)

ATOMIC_FETCH_AND_OP(and,1,b)
ATOMIC_FETCH_AND_OP(and,2,w)
ATOMIC_FETCH_AND_OP(and,4,l)

ATOMIC_FETCH_AND_OP(xor,1,b)
ATOMIC_FETCH_AND_OP(xor,2,w)
ATOMIC_FETCH_AND_OP(xor,4,l)

#define ATOMIC_FETCH_AND_COMBOP(OP,OP0,OP1,N,T) \
	.global	__sync_fetch_and_##OP##_##N; \
	HIDDEN_FUNC(__sync_fetch_and_##OP##_##N); \
	.align	2; \
__sync_fetch_and_##OP##_##N:; \
	mova	1f, r0; \
	nop; \
	mov	r15, r1; \
	mov	#(0f-1f), r15; \
0:	mov.##T	@r4, r2; \
	OP0	r2, r5; \
	OP1	r5, r5; \
	mov.##T	r5, @r4; \
1:	mov	r1, r15; \
	rts; \
	 mov	r2, r0; \
	ENDFUNC(__sync_fetch_and_##OP##_##N)

ATOMIC_FETCH_AND_COMBOP(sub,sub,neg,1,b)
ATOMIC_FETCH_AND_COMBOP(sub,sub,neg,2,w)
ATOMIC_FETCH_AND_COMBOP(sub,sub,neg,4,l)

ATOMIC_FETCH_AND_COMBOP(nand,and,not,1,b)
ATOMIC_FETCH_AND_COMBOP(nand,and,not,2,w)
ATOMIC_FETCH_AND_COMBOP(nand,and,not,4,l)

#endif /* ! __SH5__ */
