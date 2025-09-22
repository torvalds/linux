/* libgcc routines for NEC V850.
   Copyright (C) 1996, 1997, 2002, 2005 Free Software Foundation, Inc.

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

#ifdef L_mulsi3
	.text
	.globl ___mulsi3
	.type  ___mulsi3,@function
___mulsi3:
#ifdef __v850__	
/*
   #define SHIFT 12
   #define MASK ((1 << SHIFT) - 1)
    
   #define STEP(i, j)                               \
   ({                                               \
       short a_part = (a >> (i)) & MASK;            \
       short b_part = (b >> (j)) & MASK;            \
       int res = (((int) a_part) * ((int) b_part)); \
       res;                                         \
   })
  
   int
   __mulsi3 (unsigned a, unsigned b)
   {
      return STEP (0, 0) +
          ((STEP (SHIFT, 0) + STEP (0, SHIFT)) << SHIFT) +
          ((STEP (0, 2 * SHIFT) + STEP (SHIFT, SHIFT) + STEP (2 * SHIFT, 0))
           << (2 * SHIFT));
   }
*/
        mov   r6, r14
        movea lo(32767), r0, r10
        and   r10, r14
        mov   r7,  r15
        and   r10, r15
        shr   15,  r6
        mov   r6,  r13
        and   r10, r13
        shr   15,  r7
        mov   r7,  r12
        and   r10, r12
        shr   15,  r6
        shr   15,  r7
        mov   r14, r10
        mulh  r15, r10
        mov   r14, r11
        mulh  r12, r11
        mov   r13, r16
        mulh  r15, r16
        mulh  r14, r7
        mulh  r15, r6
        add   r16, r11
        mulh  r13, r12
        shl   15,  r11
        add   r11, r10
        add   r12, r7
        add   r6,  r7
        shl   30,  r7
        add   r7,  r10
        jmp   [r31]
#endif /* __v850__ */
#if defined(__v850e__) || defined(__v850ea__)
        /* This routine is almost unneccesarry because gcc
           generates the MUL instruction for the RTX mulsi3.
           But if someone wants to link his application with
           previsously compiled v850 objects then they will 
	   need this function.  */
 
        /* It isn't good to put the inst sequence as below;
              mul r7, r6,
              mov r6, r10, r0
           In this case, there is a RAW hazard between them.
           MUL inst takes 2 cycle in EX stage, then MOV inst
           must wait 1cycle.  */
        mov   r7, r10
        mul   r6, r10, r0
        jmp   [r31]
#endif /* __v850e__ */
	.size ___mulsi3,.-___mulsi3
#endif /* L_mulsi3 */


#ifdef L_udivsi3
	.text
	.global ___udivsi3
	.type	___udivsi3,@function
___udivsi3:
#ifdef __v850__
	mov 1,r12
	mov 0,r10
	cmp r6,r7
	bnl .L12
	movhi hi(-2147483648),r0,r13
	cmp r0,r7
	blt .L12
.L4:
	shl 1,r7
	shl 1,r12
	cmp r6,r7
	bnl .L12
	cmp r0,r12
	be .L8
	mov r7,r19
	and r13,r19
	be .L4
	br .L12
.L9:
	cmp r7,r6
	bl .L10
	sub r7,r6
	or r12,r10
.L10:
	shr 1,r12
	shr 1,r7
.L12:
	cmp r0,r12
	bne .L9
.L8:
	jmp [r31]

#else /* defined(__v850e__) */

	/* See comments at end of __mulsi3.  */
	mov   r6, r10	
	divu  r7, r10, r0
	jmp   [r31]		

#endif /* __v850e__ */

	.size ___udivsi3,.-___udivsi3
#endif

#ifdef L_divsi3
	.text
	.globl ___divsi3
	.type  ___divsi3,@function
___divsi3:
#ifdef __v850__
	add -8,sp
	st.w r31,4[sp]
	st.w r22,0[sp]
	mov 1,r22
	tst r7,r7
	bp .L3
	subr r0,r7
	subr r0,r22
.L3:
	tst r6,r6
	bp .L4
	subr r0,r6
	subr r0,r22
.L4:
	jarl ___udivsi3,r31
	cmp r0,r22
	bp .L7
	subr r0,r10
.L7:
	ld.w 0[sp],r22
	ld.w 4[sp],r31
	add 8,sp
	jmp [r31]

#else /* defined(__v850e__) */

	/* See comments at end of __mulsi3.  */
	mov   r6, r10
	div   r7, r10, r0
	jmp   [r31]

#endif /* __v850e__ */

	.size ___divsi3,.-___divsi3
#endif

#ifdef  L_umodsi3
	.text
	.globl ___umodsi3
	.type  ___umodsi3,@function
___umodsi3:
#ifdef __v850__
	add -12,sp
	st.w r31,8[sp]
	st.w r7,4[sp]
	st.w r6,0[sp]
	jarl ___udivsi3,r31
	ld.w 4[sp],r7
	mov r10,r6
	jarl ___mulsi3,r31
	ld.w 0[sp],r6
	subr r6,r10
	ld.w 8[sp],r31
	add 12,sp
	jmp [r31]

#else /* defined(__v850e__) */

	/* See comments at end of __mulsi3.  */
	divu  r7, r6, r10
	jmp   [r31]

#endif /* __v850e__ */

	.size ___umodsi3,.-___umodsi3
#endif /* L_umodsi3 */

#ifdef  L_modsi3
	.text
	.globl ___modsi3
	.type  ___modsi3,@function
___modsi3:
#ifdef __v850__	
	add -12,sp
	st.w r31,8[sp]
	st.w r7,4[sp]
	st.w r6,0[sp]
	jarl ___divsi3,r31
	ld.w 4[sp],r7
	mov r10,r6
	jarl ___mulsi3,r31
	ld.w 0[sp],r6
	subr r6,r10
	ld.w 8[sp],r31
	add 12,sp
	jmp [r31]

#else /* defined(__v850e__) */

	/* See comments at end of __mulsi3.  */
	div  r7, r6, r10
	jmp [r31]

#endif /* __v850e__ */

	.size ___modsi3,.-___modsi3
#endif /* L_modsi3 */

#ifdef	L_save_2
	.text
	.align	2
	.globl	__save_r2_r29
	.type	__save_r2_r29,@function
	/* Allocate space and save registers 2, 20 .. 29 on the stack */
	/* Called via:	jalr __save_r2_r29,r10 */
__save_r2_r29:
#ifdef __EP__
	mov	ep,r1
	addi	-44,sp,sp
	mov	sp,ep
	sst.w	r29,0[ep]
	sst.w	r28,4[ep]
	sst.w	r27,8[ep]
	sst.w	r26,12[ep]
	sst.w	r25,16[ep]
	sst.w	r24,20[ep]
	sst.w	r23,24[ep]
	sst.w	r22,28[ep]
	sst.w	r21,32[ep]
	sst.w	r20,36[ep]
	sst.w	r2,40[ep]
	mov	r1,ep
#else
	addi	-44,sp,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	st.w	r26,12[sp]
	st.w	r25,16[sp]
	st.w	r24,20[sp]
	st.w	r23,24[sp]
	st.w	r22,28[sp]
	st.w	r21,32[sp]
	st.w	r20,36[sp]
	st.w	r2,40[sp]
#endif
	jmp	[r10]
	.size	__save_r2_r29,.-__save_r2_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r2_r29 */
	.align	2
	.globl	__return_r2_r29
	.type	__return_r2_r29,@function
__return_r2_r29:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	0[ep],r29
	sld.w	4[ep],r28
	sld.w	8[ep],r27
	sld.w	12[ep],r26
	sld.w	16[ep],r25
	sld.w	20[ep],r24
	sld.w	24[ep],r23
	sld.w	28[ep],r22
	sld.w	32[ep],r21
	sld.w	36[ep],r20
	sld.w	40[ep],r2
	addi	44,sp,sp
	mov	r1,ep
#else
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	ld.w	8[sp],r27
	ld.w	12[sp],r26
	ld.w	16[sp],r25
	ld.w	20[sp],r24
	ld.w	24[sp],r23
	ld.w	28[sp],r22
	ld.w	32[sp],r21
	ld.w	36[sp],r20
	ld.w	40[sp],r2
	addi	44,sp,sp
#endif
	jmp	[r31]
	.size	__return_r2_r29,.-__return_r2_r29
#endif /* L_save_2 */

#ifdef	L_save_20
	.text
	.align	2
	.globl	__save_r20_r29
	.type	__save_r20_r29,@function
	/* Allocate space and save registers 20 .. 29 on the stack */
	/* Called via:	jalr __save_r20_r29,r10 */
__save_r20_r29:
#ifdef __EP__
	mov	ep,r1
	addi	-40,sp,sp
	mov	sp,ep
	sst.w	r29,0[ep]
	sst.w	r28,4[ep]
	sst.w	r27,8[ep]
	sst.w	r26,12[ep]
	sst.w	r25,16[ep]
	sst.w	r24,20[ep]
	sst.w	r23,24[ep]
	sst.w	r22,28[ep]
	sst.w	r21,32[ep]
	sst.w	r20,36[ep]
	mov	r1,ep
#else
	addi	-40,sp,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	st.w	r26,12[sp]
	st.w	r25,16[sp]
	st.w	r24,20[sp]
	st.w	r23,24[sp]
	st.w	r22,28[sp]
	st.w	r21,32[sp]
	st.w	r20,36[sp]
#endif
	jmp	[r10]
	.size	__save_r20_r29,.-__save_r20_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r20_r29 */
	.align	2
	.globl	__return_r20_r29
	.type	__return_r20_r29,@function
__return_r20_r29:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	0[ep],r29
	sld.w	4[ep],r28
	sld.w	8[ep],r27
	sld.w	12[ep],r26
	sld.w	16[ep],r25
	sld.w	20[ep],r24
	sld.w	24[ep],r23
	sld.w	28[ep],r22
	sld.w	32[ep],r21
	sld.w	36[ep],r20
	addi	40,sp,sp
	mov	r1,ep
#else
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	ld.w	8[sp],r27
	ld.w	12[sp],r26
	ld.w	16[sp],r25
	ld.w	20[sp],r24
	ld.w	24[sp],r23
	ld.w	28[sp],r22
	ld.w	32[sp],r21
	ld.w	36[sp],r20
	addi	40,sp,sp
#endif
	jmp	[r31]
	.size	__return_r20_r29,.-__return_r20_r29
#endif /* L_save_20 */

#ifdef	L_save_21
	.text
	.align	2
	.globl	__save_r21_r29
	.type	__save_r21_r29,@function
	/* Allocate space and save registers 21 .. 29 on the stack */
	/* Called via:	jalr __save_r21_r29,r10 */
__save_r21_r29:
#ifdef __EP__
	mov	ep,r1
	addi	-36,sp,sp
	mov	sp,ep
	sst.w	r29,0[ep]
	sst.w	r28,4[ep]
	sst.w	r27,8[ep]
	sst.w	r26,12[ep]
	sst.w	r25,16[ep]
	sst.w	r24,20[ep]
	sst.w	r23,24[ep]
	sst.w	r22,28[ep]
	sst.w	r21,32[ep]
	mov	r1,ep
#else
	addi	-36,sp,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	st.w	r26,12[sp]
	st.w	r25,16[sp]
	st.w	r24,20[sp]
	st.w	r23,24[sp]
	st.w	r22,28[sp]
	st.w	r21,32[sp]
#endif
	jmp	[r10]
	.size	__save_r21_r29,.-__save_r21_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r21_r29 */
	.align	2
	.globl	__return_r21_r29
	.type	__return_r21_r29,@function
__return_r21_r29:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	0[ep],r29
	sld.w	4[ep],r28
	sld.w	8[ep],r27
	sld.w	12[ep],r26
	sld.w	16[ep],r25
	sld.w	20[ep],r24
	sld.w	24[ep],r23
	sld.w	28[ep],r22
	sld.w	32[ep],r21
	addi	36,sp,sp
	mov	r1,ep
#else
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	ld.w	8[sp],r27
	ld.w	12[sp],r26
	ld.w	16[sp],r25
	ld.w	20[sp],r24
	ld.w	24[sp],r23
	ld.w	28[sp],r22
	ld.w	32[sp],r21
	addi	36,sp,sp
#endif
	jmp	[r31]
	.size	__return_r21_r29,.-__return_r21_r29
#endif /* L_save_21 */

#ifdef	L_save_22
	.text
	.align	2
	.globl	__save_r22_r29
	.type	__save_r22_r29,@function
	/* Allocate space and save registers 22 .. 29 on the stack */
	/* Called via:	jalr __save_r22_r29,r10 */
__save_r22_r29:
#ifdef __EP__
	mov	ep,r1
	addi	-32,sp,sp
	mov	sp,ep
	sst.w	r29,0[ep]
	sst.w	r28,4[ep]
	sst.w	r27,8[ep]
	sst.w	r26,12[ep]
	sst.w	r25,16[ep]
	sst.w	r24,20[ep]
	sst.w	r23,24[ep]
	sst.w	r22,28[ep]
	mov	r1,ep
#else
	addi	-32,sp,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	st.w	r26,12[sp]
	st.w	r25,16[sp]
	st.w	r24,20[sp]
	st.w	r23,24[sp]
	st.w	r22,28[sp]
#endif
	jmp	[r10]
	.size	__save_r22_r29,.-__save_r22_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r22_r29 */
	.align	2
	.globl	__return_r22_r29
	.type	__return_r22_r29,@function
__return_r22_r29:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	0[ep],r29
	sld.w	4[ep],r28
	sld.w	8[ep],r27
	sld.w	12[ep],r26
	sld.w	16[ep],r25
	sld.w	20[ep],r24
	sld.w	24[ep],r23
	sld.w	28[ep],r22
	addi	32,sp,sp
	mov	r1,ep
#else
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	ld.w	8[sp],r27
	ld.w	12[sp],r26
	ld.w	16[sp],r25
	ld.w	20[sp],r24
	ld.w	24[sp],r23
	ld.w	28[sp],r22
	addi	32,sp,sp
#endif
	jmp	[r31]
	.size	__return_r22_r29,.-__return_r22_r29
#endif /* L_save_22 */

#ifdef	L_save_23
	.text
	.align	2
	.globl	__save_r23_r29
	.type	__save_r23_r29,@function
	/* Allocate space and save registers 23 .. 29 on the stack */
	/* Called via:	jalr __save_r23_r29,r10 */
__save_r23_r29:
#ifdef __EP__
	mov	ep,r1
	addi	-28,sp,sp
	mov	sp,ep
	sst.w	r29,0[ep]
	sst.w	r28,4[ep]
	sst.w	r27,8[ep]
	sst.w	r26,12[ep]
	sst.w	r25,16[ep]
	sst.w	r24,20[ep]
	sst.w	r23,24[ep]
	mov	r1,ep
#else
	addi	-28,sp,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	st.w	r26,12[sp]
	st.w	r25,16[sp]
	st.w	r24,20[sp]
	st.w	r23,24[sp]
#endif
	jmp	[r10]
	.size	__save_r23_r29,.-__save_r23_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r23_r29 */
	.align	2
	.globl	__return_r23_r29
	.type	__return_r23_r29,@function
__return_r23_r29:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	0[ep],r29
	sld.w	4[ep],r28
	sld.w	8[ep],r27
	sld.w	12[ep],r26
	sld.w	16[ep],r25
	sld.w	20[ep],r24
	sld.w	24[ep],r23
	addi	28,sp,sp
	mov	r1,ep
#else
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	ld.w	8[sp],r27
	ld.w	12[sp],r26
	ld.w	16[sp],r25
	ld.w	20[sp],r24
	ld.w	24[sp],r23
	addi	28,sp,sp
#endif
	jmp	[r31]
	.size	__return_r23_r29,.-__return_r23_r29
#endif /* L_save_23 */

#ifdef	L_save_24
	.text
	.align	2
	.globl	__save_r24_r29
	.type	__save_r24_r29,@function
	/* Allocate space and save registers 24 .. 29 on the stack */
	/* Called via:	jalr __save_r24_r29,r10 */
__save_r24_r29:
#ifdef __EP__
	mov	ep,r1
	addi	-24,sp,sp
	mov	sp,ep
	sst.w	r29,0[ep]
	sst.w	r28,4[ep]
	sst.w	r27,8[ep]
	sst.w	r26,12[ep]
	sst.w	r25,16[ep]
	sst.w	r24,20[ep]
	mov	r1,ep
#else
	addi	-24,sp,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	st.w	r26,12[sp]
	st.w	r25,16[sp]
	st.w	r24,20[sp]
#endif
	jmp	[r10]
	.size	__save_r24_r29,.-__save_r24_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r24_r29 */
	.align	2
	.globl	__return_r24_r29
	.type	__return_r24_r29,@function
__return_r24_r29:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	0[ep],r29
	sld.w	4[ep],r28
	sld.w	8[ep],r27
	sld.w	12[ep],r26
	sld.w	16[ep],r25
	sld.w	20[ep],r24
	addi	24,sp,sp
	mov	r1,ep
#else
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	ld.w	8[sp],r27
	ld.w	12[sp],r26
	ld.w	16[sp],r25
	ld.w	20[sp],r24
	addi	24,sp,sp
#endif
	jmp	[r31]
	.size	__return_r24_r29,.-__return_r24_r29
#endif /* L_save_24 */

#ifdef	L_save_25
	.text
	.align	2
	.globl	__save_r25_r29
	.type	__save_r25_r29,@function
	/* Allocate space and save registers 25 .. 29 on the stack */
	/* Called via:	jalr __save_r25_r29,r10 */
__save_r25_r29:
#ifdef __EP__
	mov	ep,r1
	addi	-20,sp,sp
	mov	sp,ep
	sst.w	r29,0[ep]
	sst.w	r28,4[ep]
	sst.w	r27,8[ep]
	sst.w	r26,12[ep]
	sst.w	r25,16[ep]
	mov	r1,ep
#else
	addi	-20,sp,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	st.w	r26,12[sp]
	st.w	r25,16[sp]
#endif
	jmp	[r10]
	.size	__save_r25_r29,.-__save_r25_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r25_r29 */
	.align	2
	.globl	__return_r25_r29
	.type	__return_r25_r29,@function
__return_r25_r29:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	0[ep],r29
	sld.w	4[ep],r28
	sld.w	8[ep],r27
	sld.w	12[ep],r26
	sld.w	16[ep],r25
	addi	20,sp,sp
	mov	r1,ep
#else
	ld.w	0[ep],r29
	ld.w	4[ep],r28
	ld.w	8[ep],r27
	ld.w	12[ep],r26
	ld.w	16[ep],r25
	addi	20,sp,sp
#endif
	jmp	[r31]
	.size	__return_r25_r29,.-__return_r25_r29
#endif /* L_save_25 */

#ifdef	L_save_26
	.text
	.align	2
	.globl	__save_r26_r29
	.type	__save_r26_r29,@function
	/* Allocate space and save registers 26 .. 29 on the stack */
	/* Called via:	jalr __save_r26_r29,r10 */
__save_r26_r29:
#ifdef __EP__
	mov	ep,r1
	add	-16,sp
	mov	sp,ep
	sst.w	r29,0[ep]
	sst.w	r28,4[ep]
	sst.w	r27,8[ep]
	sst.w	r26,12[ep]
	mov	r1,ep
#else
	add	-16,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	st.w	r26,12[sp]
#endif
	jmp	[r10]
	.size	__save_r26_r29,.-__save_r26_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r26_r29 */
	.align	2
	.globl	__return_r26_r29
	.type	__return_r26_r29,@function
__return_r26_r29:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	0[ep],r29
	sld.w	4[ep],r28
	sld.w	8[ep],r27
	sld.w	12[ep],r26
	addi	16,sp,sp
	mov	r1,ep
#else
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	ld.w	8[sp],r27
	ld.w	12[sp],r26
	addi	16,sp,sp
#endif
	jmp	[r31]
	.size	__return_r26_r29,.-__return_r26_r29
#endif /* L_save_26 */

#ifdef	L_save_27
	.text
	.align	2
	.globl	__save_r27_r29
	.type	__save_r27_r29,@function
	/* Allocate space and save registers 27 .. 29 on the stack */
	/* Called via:	jalr __save_r27_r29,r10 */
__save_r27_r29:
	add	-12,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	st.w	r27,8[sp]
	jmp	[r10]
	.size	__save_r27_r29,.-__save_r27_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r27_r29 */
	.align	2
	.globl	__return_r27_r29
	.type	__return_r27_r29,@function
__return_r27_r29:
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	ld.w	8[sp],r27
	add	12,sp
	jmp	[r31]
	.size	__return_r27_r29,.-__return_r27_r29
#endif /* L_save_27 */

#ifdef	L_save_28
	.text
	.align	2
	.globl	__save_r28_r29
	.type	__save_r28_r29,@function
	/* Allocate space and save registers 28,29 on the stack */
	/* Called via:	jalr __save_r28_r29,r10 */
__save_r28_r29:
	add	-8,sp
	st.w	r29,0[sp]
	st.w	r28,4[sp]
	jmp	[r10]
	.size	__save_r28_r29,.-__save_r28_r29

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r28_r29 */
	.align	2
	.globl	__return_r28_r29
	.type	__return_r28_r29,@function
__return_r28_r29:
	ld.w	0[sp],r29
	ld.w	4[sp],r28
	add	8,sp
	jmp	[r31]
	.size	__return_r28_r29,.-__return_r28_r29
#endif /* L_save_28 */

#ifdef	L_save_29
	.text
	.align	2
	.globl	__save_r29
	.type	__save_r29,@function
	/* Allocate space and save register 29 on the stack */
	/* Called via:	jalr __save_r29,r10 */
__save_r29:
	add	-4,sp
	st.w	r29,0[sp]
	jmp	[r10]
	.size	__save_r29,.-__save_r29

	/* Restore saved register 29, deallocate stack and return to the user */
	/* Called via:	jr __return_r29 */
	.align	2
	.globl	__return_r29
	.type	__return_r29,@function
__return_r29:
	ld.w	0[sp],r29
	add	4,sp
	jmp	[r31]
	.size	__return_r29,.-__return_r29
#endif /* L_save_28 */

#ifdef	L_save_2c
	.text
	.align	2
	.globl	__save_r2_r31
	.type	__save_r2_r31,@function
	/* Allocate space and save registers 20 .. 29, 31 on the stack.  */
	/* Also allocate space for the argument save area.  */
	/* Called via:	jalr __save_r2_r31,r10.  */
__save_r2_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-64,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r26,28[ep]
	sst.w	r25,32[ep]
	sst.w	r24,36[ep]
	sst.w	r23,40[ep]
	sst.w	r22,44[ep]
	sst.w	r21,48[ep]
	sst.w	r20,52[ep]
	sst.w	r2,56[ep]
	sst.w	r31,60[ep]
	mov	r1,ep
#else
	addi	-64,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r26,28[sp]
	st.w	r25,32[sp]
	st.w	r24,36[sp]
	st.w	r23,40[sp]
	st.w	r22,44[sp]
	st.w	r21,48[sp]
	st.w	r20,52[sp]
	st.w	r2,56[sp]
	st.w	r31,60[sp]
#endif
	jmp	[r10]
	.size	__save_r2_r31,.-__save_r2_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r20_r31 */
	.align	2
	.globl	__return_r2_r31
	.type	__return_r2_r31,@function
__return_r2_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r26
	sld.w	32[ep],r25
	sld.w	36[ep],r24
	sld.w	40[ep],r23
	sld.w	44[ep],r22
	sld.w	48[ep],r21
	sld.w	52[ep],r20
	sld.w	56[ep],r2
	sld.w	60[ep],r31
	addi	64,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r26
	ld.w	32[sp],r25
	ld.w	36[sp],r24
	ld.w	40[sp],r23
	ld.w	44[sp],r22
	ld.w	48[sp],r21
	ld.w	52[sp],r20
	ld.w	56[sp],r2
	ld.w	60[sp],r31
	addi	64,sp,sp
#endif
	jmp	[r31]
	.size	__return_r2_r31,.-__return_r2_r31
#endif /* L_save_2c */

#ifdef	L_save_20c
	.text
	.align	2
	.globl	__save_r20_r31
	.type	__save_r20_r31,@function
	/* Allocate space and save registers 20 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r20_r31,r10 */
__save_r20_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-60,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r26,28[ep]
	sst.w	r25,32[ep]
	sst.w	r24,36[ep]
	sst.w	r23,40[ep]
	sst.w	r22,44[ep]
	sst.w	r21,48[ep]
	sst.w	r20,52[ep]
	sst.w	r31,56[ep]
	mov	r1,ep
#else
	addi	-60,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r26,28[sp]
	st.w	r25,32[sp]
	st.w	r24,36[sp]
	st.w	r23,40[sp]
	st.w	r22,44[sp]
	st.w	r21,48[sp]
	st.w	r20,52[sp]
	st.w	r31,56[sp]
#endif
	jmp	[r10]
	.size	__save_r20_r31,.-__save_r20_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r20_r31 */
	.align	2
	.globl	__return_r20_r31
	.type	__return_r20_r31,@function
__return_r20_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r26
	sld.w	32[ep],r25
	sld.w	36[ep],r24
	sld.w	40[ep],r23
	sld.w	44[ep],r22
	sld.w	48[ep],r21
	sld.w	52[ep],r20
	sld.w	56[ep],r31
	addi	60,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r26
	ld.w	32[sp],r25
	ld.w	36[sp],r24
	ld.w	40[sp],r23
	ld.w	44[sp],r22
	ld.w	48[sp],r21
	ld.w	52[sp],r20
	ld.w	56[sp],r31
	addi	60,sp,sp
#endif
	jmp	[r31]
	.size	__return_r20_r31,.-__return_r20_r31
#endif /* L_save_20c */

#ifdef	L_save_21c
	.text
	.align	2
	.globl	__save_r21_r31
	.type	__save_r21_r31,@function
	/* Allocate space and save registers 21 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r21_r31,r10 */
__save_r21_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-56,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r26,28[ep]
	sst.w	r25,32[ep]
	sst.w	r24,36[ep]
	sst.w	r23,40[ep]
	sst.w	r22,44[ep]
	sst.w	r21,48[ep]
	sst.w	r31,52[ep]
	mov	r1,ep
#else
	addi	-56,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r26,28[sp]
	st.w	r25,32[sp]
	st.w	r24,36[sp]
	st.w	r23,40[sp]
	st.w	r22,44[sp]
	st.w	r21,48[sp]
	st.w	r31,52[sp]
#endif
	jmp	[r10]
	.size	__save_r21_r31,.-__save_r21_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r21_r31 */
	.align	2
	.globl	__return_r21_r31
	.type	__return_r21_r31,@function
__return_r21_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r26
	sld.w	32[ep],r25
	sld.w	36[ep],r24
	sld.w	40[ep],r23
	sld.w	44[ep],r22
	sld.w	48[ep],r21
	sld.w	52[ep],r31
	addi	56,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r26
	ld.w	32[sp],r25
	ld.w	36[sp],r24
	ld.w	40[sp],r23
	ld.w	44[sp],r22
	ld.w	48[sp],r21
	ld.w	52[sp],r31
	addi	56,sp,sp
#endif
	jmp	[r31]
	.size	__return_r21_r31,.-__return_r21_r31
#endif /* L_save_21c */

#ifdef	L_save_22c
	.text
	.align	2
	.globl	__save_r22_r31
	.type	__save_r22_r31,@function
	/* Allocate space and save registers 22 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r22_r31,r10 */
__save_r22_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-52,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r26,28[ep]
	sst.w	r25,32[ep]
	sst.w	r24,36[ep]
	sst.w	r23,40[ep]
	sst.w	r22,44[ep]
	sst.w	r31,48[ep]
	mov	r1,ep
#else
	addi	-52,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r26,28[sp]
	st.w	r25,32[sp]
	st.w	r24,36[sp]
	st.w	r23,40[sp]
	st.w	r22,44[sp]
	st.w	r31,48[sp]
#endif
	jmp	[r10]
	.size	__save_r22_r31,.-__save_r22_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r22_r31 */
	.align	2
	.globl	__return_r22_r31
	.type	__return_r22_r31,@function
__return_r22_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r26
	sld.w	32[ep],r25
	sld.w	36[ep],r24
	sld.w	40[ep],r23
	sld.w	44[ep],r22
	sld.w	48[ep],r31
	addi	52,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r26
	ld.w	32[sp],r25
	ld.w	36[sp],r24
	ld.w	40[sp],r23
	ld.w	44[sp],r22
	ld.w	48[sp],r31
	addi	52,sp,sp
#endif
	jmp	[r31]
	.size	__return_r22_r31,.-__return_r22_r31
#endif /* L_save_22c */

#ifdef	L_save_23c
	.text
	.align	2
	.globl	__save_r23_r31
	.type	__save_r23_r31,@function
	/* Allocate space and save registers 23 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r23_r31,r10 */
__save_r23_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-48,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r26,28[ep]
	sst.w	r25,32[ep]
	sst.w	r24,36[ep]
	sst.w	r23,40[ep]
	sst.w	r31,44[ep]
	mov	r1,ep
#else
	addi	-48,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r26,28[sp]
	st.w	r25,32[sp]
	st.w	r24,36[sp]
	st.w	r23,40[sp]
	st.w	r31,44[sp]
#endif
	jmp	[r10]
	.size	__save_r23_r31,.-__save_r23_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r23_r31 */
	.align	2
	.globl	__return_r23_r31
	.type	__return_r23_r31,@function
__return_r23_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r26
	sld.w	32[ep],r25
	sld.w	36[ep],r24
	sld.w	40[ep],r23
	sld.w	44[ep],r31
	addi	48,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r26
	ld.w	32[sp],r25
	ld.w	36[sp],r24
	ld.w	40[sp],r23
	ld.w	44[sp],r31
	addi	48,sp,sp
#endif
	jmp	[r31]
	.size	__return_r23_r31,.-__return_r23_r31
#endif /* L_save_23c */

#ifdef	L_save_24c
	.text
	.align	2
	.globl	__save_r24_r31
	.type	__save_r24_r31,@function
	/* Allocate space and save registers 24 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r24_r31,r10 */
__save_r24_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-44,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r26,28[ep]
	sst.w	r25,32[ep]
	sst.w	r24,36[ep]
	sst.w	r31,40[ep]
	mov	r1,ep
#else
	addi	-44,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r26,28[sp]
	st.w	r25,32[sp]
	st.w	r24,36[sp]
	st.w	r31,40[sp]
#endif
	jmp	[r10]
	.size	__save_r24_r31,.-__save_r24_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r24_r31 */
	.align	2
	.globl	__return_r24_r31
	.type	__return_r24_r31,@function
__return_r24_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r26
	sld.w	32[ep],r25
	sld.w	36[ep],r24
	sld.w	40[ep],r31
	addi	44,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r26
	ld.w	32[sp],r25
	ld.w	36[sp],r24
	ld.w	40[sp],r31
	addi	44,sp,sp
#endif
	jmp	[r31]
	.size	__return_r24_r31,.-__return_r24_r31
#endif /* L_save_24c */

#ifdef	L_save_25c
	.text
	.align	2
	.globl	__save_r25_r31
	.type	__save_r25_r31,@function
	/* Allocate space and save registers 25 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r25_r31,r10 */
__save_r25_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-40,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r26,28[ep]
	sst.w	r25,32[ep]
	sst.w	r31,36[ep]
	mov	r1,ep
#else
	addi	-40,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r26,28[sp]
	st.w	r25,32[sp]
	st.w	r31,36[sp]
#endif
	jmp	[r10]
	.size	__save_r25_r31,.-__save_r25_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r25_r31 */
	.align	2
	.globl	__return_r25_r31
	.type	__return_r25_r31,@function
__return_r25_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r26
	sld.w	32[ep],r25
	sld.w	36[ep],r31
	addi	40,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r26
	ld.w	32[sp],r25
	ld.w	36[sp],r31
	addi	40,sp,sp
#endif
	jmp	[r31]
	.size	__return_r25_r31,.-__return_r25_r31
#endif /* L_save_25c */

#ifdef	L_save_26c
	.text
	.align	2
	.globl	__save_r26_r31
	.type	__save_r26_r31,@function
	/* Allocate space and save registers 26 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r26_r31,r10 */
__save_r26_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-36,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r26,28[ep]
	sst.w	r31,32[ep]
	mov	r1,ep
#else
	addi	-36,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r26,28[sp]
	st.w	r31,32[sp]
#endif
	jmp	[r10]
	.size	__save_r26_r31,.-__save_r26_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r26_r31 */
	.align	2
	.globl	__return_r26_r31
	.type	__return_r26_r31,@function
__return_r26_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r26
	sld.w	32[ep],r31
	addi	36,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r26
	ld.w	32[sp],r31
	addi	36,sp,sp
#endif
	jmp	[r31]
	.size	__return_r26_r31,.-__return_r26_r31
#endif /* L_save_26c */

#ifdef	L_save_27c
	.text
	.align	2
	.globl	__save_r27_r31
	.type	__save_r27_r31,@function
	/* Allocate space and save registers 27 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r27_r31,r10 */
__save_r27_r31:
#ifdef __EP__
	mov	ep,r1
	addi	-32,sp,sp
	mov	sp,ep
	sst.w	r29,16[ep]
	sst.w	r28,20[ep]
	sst.w	r27,24[ep]
	sst.w	r31,28[ep]
	mov	r1,ep
#else
	addi	-32,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r27,24[sp]
	st.w	r31,28[sp]
#endif
	jmp	[r10]
	.size	__save_r27_r31,.-__save_r27_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r27_r31 */
	.align	2
	.globl	__return_r27_r31
	.type	__return_r27_r31,@function
__return_r27_r31:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	16[ep],r29
	sld.w	20[ep],r28
	sld.w	24[ep],r27
	sld.w	28[ep],r31
	addi	32,sp,sp
	mov	r1,ep
#else
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r27
	ld.w	28[sp],r31
	addi	32,sp,sp
#endif
	jmp	[r31]
	.size	__return_r27_r31,.-__return_r27_r31
#endif /* L_save_27c */

#ifdef	L_save_28c
	.text
	.align	2
	.globl	__save_r28_r31
	.type	__save_r28_r31,@function
	/* Allocate space and save registers 28 .. 29, 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r28_r31,r10 */
__save_r28_r31:
	addi	-28,sp,sp
	st.w	r29,16[sp]
	st.w	r28,20[sp]
	st.w	r31,24[sp]
	jmp	[r10]
	.size	__save_r28_r31,.-__save_r28_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r28_r31 */
	.align	2
	.globl	__return_r28_r31
	.type	__return_r28_r31,@function
__return_r28_r31:
	ld.w	16[sp],r29
	ld.w	20[sp],r28
	ld.w	24[sp],r31
	addi	28,sp,sp
	jmp	[r31]
	.size	__return_r28_r31,.-__return_r28_r31
#endif /* L_save_28c */

#ifdef	L_save_29c
	.text
	.align	2
	.globl	__save_r29_r31
	.type	__save_r29_r31,@function
	/* Allocate space and save registers 29 & 31 on the stack */
	/* Also allocate space for the argument save area */
	/* Called via:	jalr __save_r29_r31,r10 */
__save_r29_r31:
	addi	-24,sp,sp
	st.w	r29,16[sp]
	st.w	r31,20[sp]
	jmp	[r10]
	.size	__save_r29_r31,.-__save_r29_r31

	/* Restore saved registers, deallocate stack and return to the user */
	/* Called via:	jr __return_r29_r31 */
	.align	2
	.globl	__return_r29_r31
	.type	__return_r29_r31,@function
__return_r29_r31:
	ld.w	16[sp],r29
	ld.w	20[sp],r31
	addi	24,sp,sp
	jmp	[r31]
	.size	__return_r29_r31,.-__return_r29_r31
#endif /* L_save_29c */

#ifdef	L_save_31c
	.text
	.align	2
	.globl	__save_r31
	.type	__save_r31,@function
	/* Allocate space and save register 31 on the stack.  */
	/* Also allocate space for the argument save area.  */
	/* Called via:	jalr __save_r31,r10 */
__save_r31:
	addi	-20,sp,sp
	st.w	r31,16[sp]
	jmp	[r10]
	.size	__save_r31,.-__save_r31

	/* Restore saved registers, deallocate stack and return to the user.  */
	/* Called via:	jr __return_r31 */
	.align	2
	.globl	__return_r31
	.type	__return_r31,@function
__return_r31:
	ld.w	16[sp],r31
	addi	20,sp,sp
	jmp	[r31]
        .size   __return_r31,.-__return_r31
#endif /* L_save_31c */

#ifdef L_save_varargs
	.text
	.align	2
	.globl	__save_r6_r9
	.type	__save_r6_r9,@function
	/* Save registers 6 .. 9 on the stack for variable argument functions.  */
	/* Called via:	jalr __save_r6_r9,r10 */
__save_r6_r9:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sst.w	r6,0[ep]
	sst.w	r7,4[ep]
	sst.w	r8,8[ep]
	sst.w	r9,12[ep]
	mov	r1,ep
#else
	st.w	r6,0[sp]
	st.w	r7,4[sp]
	st.w	r8,8[sp]
	st.w	r9,12[sp]
#endif
	jmp	[r10]
	.size	__save_r6_r9,.-__save_r6_r9
#endif /* L_save_varargs */

#ifdef	L_save_interrupt
	.text
	.align	2
	.globl	__save_interrupt
	.type	__save_interrupt,@function
	/* Save registers r1, r4 on stack and load up with expected values.  */
	/* Note, 12 bytes of stack have already been allocated.  */
	/* Called via:	jalr __save_interrupt,r10 */
__save_interrupt:
	st.w	ep,0[sp]
	st.w	gp,4[sp]
	st.w	r1,8[sp]
	movhi	hi(__ep),r0,ep
	movea	lo(__ep),ep,ep
	movhi	hi(__gp),r0,gp
	movea	lo(__gp),gp,gp
	jmp	[r10]
	.size	__save_interrupt,.-__save_interrupt

	/* Restore saved registers, deallocate stack and return from the interrupt.  */
	/* Called via:	jr __return_interrupt */
	.align	2
	.globl	__return_interrupt
	.type	__return_interrupt,@function
__return_interrupt:
	ld.w	0[sp],ep
	ld.w	4[sp],gp
	ld.w	8[sp],r1
	ld.w	12[sp],r10
	addi	16,sp,sp
	reti
	.size	__return_interrupt,.-__return_interrupt
#endif /* L_save_interrupt */

#ifdef L_save_all_interrupt
	.text
	.align	2
	.globl	__save_all_interrupt
	.type	__save_all_interrupt,@function
	/* Save all registers except for those saved in __save_interrupt.  */
	/* Allocate enough stack for all of the registers & 16 bytes of space.  */
	/* Called via:	jalr __save_all_interrupt,r10 */
__save_all_interrupt:
	addi	-120,sp,sp
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sst.w	r31,116[ep]
	sst.w	r2,112[ep]
	sst.w	gp,108[ep]
	sst.w	r6,104[ep]
	sst.w	r7,100[ep]
	sst.w	r8,96[ep]
	sst.w	r9,92[ep]
	sst.w	r11,88[ep]
	sst.w	r12,84[ep]
	sst.w	r13,80[ep]
	sst.w	r14,76[ep]
	sst.w	r15,72[ep]
	sst.w	r16,68[ep]
	sst.w	r17,64[ep]
	sst.w	r18,60[ep]
	sst.w	r19,56[ep]
	sst.w	r20,52[ep]
	sst.w	r21,48[ep]
	sst.w	r22,44[ep]
	sst.w	r23,40[ep]
	sst.w	r24,36[ep]
	sst.w	r25,32[ep]
	sst.w	r26,28[ep]
	sst.w	r27,24[ep]
	sst.w	r28,20[ep]
	sst.w	r29,16[ep]
	mov	r1,ep
#else
	st.w	r31,116[sp]
	st.w	r2,112[sp]
	st.w	gp,108[sp]
	st.w	r6,104[sp]
	st.w	r7,100[sp]
	st.w	r8,96[sp]
	st.w	r9,92[sp]
	st.w	r11,88[sp]
	st.w	r12,84[sp]
	st.w	r13,80[sp]
	st.w	r14,76[sp]
	st.w	r15,72[sp]
	st.w	r16,68[sp]
	st.w	r17,64[sp]
	st.w	r18,60[sp]
	st.w	r19,56[sp]
	st.w	r20,52[sp]
	st.w	r21,48[sp]
	st.w	r22,44[sp]
	st.w	r23,40[sp]
	st.w	r24,36[sp]
	st.w	r25,32[sp]
	st.w	r26,28[sp]
	st.w	r27,24[sp]
	st.w	r28,20[sp]
	st.w	r29,16[sp]
#endif
	jmp	[r10]
	.size	__save_all_interrupt,.-__save_all_interrupt

	.globl	__restore_all_interrupt
	.type	__restore_all_interrupt,@function
	/* Restore all registers saved in __save_all_interrupt and
	   deallocate the stack space.  */
	/* Called via:	jalr __restore_all_interrupt,r10 */
__restore_all_interrupt:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sld.w	116[ep],r31
	sld.w	112[ep],r2
	sld.w	108[ep],gp
	sld.w	104[ep],r6
	sld.w	100[ep],r7
	sld.w	96[ep],r8
	sld.w	92[ep],r9
	sld.w	88[ep],r11
	sld.w	84[ep],r12
	sld.w	80[ep],r13
	sld.w	76[ep],r14
	sld.w	72[ep],r15
	sld.w	68[ep],r16
	sld.w	64[ep],r17
	sld.w	60[ep],r18
	sld.w	56[ep],r19
	sld.w	52[ep],r20
	sld.w	48[ep],r21
	sld.w	44[ep],r22
	sld.w	40[ep],r23
	sld.w	36[ep],r24
	sld.w	32[ep],r25
	sld.w	28[ep],r26
	sld.w	24[ep],r27
	sld.w	20[ep],r28
	sld.w	16[ep],r29
	mov	r1,ep
#else
	ld.w	116[sp],r31
	ld.w	112[sp],r2
	ld.w	108[sp],gp
	ld.w	104[sp],r6
	ld.w	100[sp],r7
	ld.w	96[sp],r8
	ld.w	92[sp],r9
	ld.w	88[sp],r11
	ld.w	84[sp],r12
	ld.w	80[sp],r13
	ld.w	76[sp],r14
	ld.w	72[sp],r15
	ld.w	68[sp],r16
	ld.w	64[sp],r17
	ld.w	60[sp],r18
	ld.w	56[sp],r19
	ld.w	52[sp],r20
	ld.w	48[sp],r21
	ld.w	44[sp],r22
	ld.w	40[sp],r23
	ld.w	36[sp],r24
	ld.w	32[sp],r25
	ld.w	28[sp],r26
	ld.w	24[sp],r27
	ld.w	20[sp],r28
	ld.w	16[sp],r29
#endif
	addi	120,sp,sp	
	jmp	[r10]
	.size	__restore_all_interrupt,.-__restore_all_interrupt
#endif /* L_save_all_interrupt */

	
#if defined __v850e__
#ifdef	L_callt_save_r2_r29
	/* Put these functions into the call table area.  */
	.call_table_text
	
	/* Allocate space and save registers 2, 20 .. 29 on the stack.  */
	/* Called via:	callt ctoff(__callt_save_r2_r29).  */
	.align	2
.L_save_r2_r29:
	add	-4, sp
	st.w	r2, 0[sp]
	prepare {r20 - r29}, 0
	ctret

	/* Restore saved registers, deallocate stack and return to the user.  */
	/* Called via:	callt ctoff(__callt_return_r2_r29).  */
	.align	2
.L_return_r2_r29:
	dispose 0, {r20-r29}
	ld.w    0[sp], r2
	add	4, sp
	jmp     [r31]

	/* Place the offsets of the start of these routines into the call table.  */
	.call_table_data

	.global	__callt_save_r2_r29
	.type	__callt_save_r2_r29,@function
__callt_save_r2_r29:	.short ctoff(.L_save_r2_r29)
	
	.global	__callt_return_r2_r29
	.type	__callt_return_r2_r29,@function
__callt_return_r2_r29:	.short ctoff(.L_return_r2_r29)
	
#endif /* L_callt_save_r2_r29 */

#ifdef	L_callt_save_r2_r31
	/* Put these functions into the call table area.  */
	.call_table_text
	
	/* Allocate space and save registers 2 and 20 .. 29, 31 on the stack.  */
	/* Also allocate space for the argument save area.  */
	/* Called via:	callt ctoff(__callt_save_r2_r31).  */
	.align	2
.L_save_r2_r31:
	add	-4, sp
	st.w	r2, 0[sp]
	prepare {r20 - r29, r31}, 4
	ctret

	/* Restore saved registers, deallocate stack and return to the user.  */
	/* Called via:	callt ctoff(__callt_return_r2_r31).  */
	.align	2
.L_return_r2_r31:
	dispose 4, {r20 - r29, r31}
	ld.w    0[sp], r2
	addi	4, sp, sp
	jmp     [r31]

	/* Place the offsets of the start of these routines into the call table.  */
	.call_table_data

	.global	__callt_save_r2_r31
	.type	__callt_save_r2_r31,@function
__callt_save_r2_r31:	.short ctoff(.L_save_r2_r31)
	
	.global	__callt_return_r2_r31
	.type	__callt_return_r2_r31,@function
__callt_return_r2_r31:	.short ctoff(.L_return_r2_r31)
	
#endif /* L_callt_save_r2_r31 */


#ifdef L_callt_save_r6_r9
	/* Put these functions into the call table area.  */
	.call_table_text
	
	/* Save registers r6 - r9 onto the stack in the space reserved for them.
	   Use by variable argument functions. 
	   Called via:	callt ctoff(__callt_save_r6_r9).  */
	.align	2
.L_save_r6_r9:
#ifdef __EP__
	mov	ep,r1
	mov	sp,ep
	sst.w	r6,0[ep]
	sst.w	r7,4[ep]
	sst.w	r8,8[ep]
	sst.w	r9,12[ep]
	mov	r1,ep
#else
	st.w	r6,0[sp]
	st.w	r7,4[sp]
	st.w	r8,8[sp]
	st.w	r9,12[sp]
#endif
	ctret

	/* Place the offsets of the start of this routines into the call table.  */
	.call_table_data

	.global	__callt_save_r6_r9
	.type	__callt_save_r6_r9,@function
__callt_save_r6_r9:	.short ctoff(.L_save_r6_r9)
#endif /* L_callt_save_r6_r9 */

	
#ifdef	L_callt_save_interrupt
	/* Put these functions into the call table area.  */
	.call_table_text
	
	/* Save registers r1, ep, gp, r10 on stack and load up with expected values.  */
	/* Called via:	callt ctoff(__callt_save_interrupt).  */
	.align	2
.L_save_interrupt:
        /* SP has already been moved before callt ctoff(_save_interrupt).  */
        /* addi -24, sp, sp  */
        st.w    ep,  0[sp]
        st.w    gp,  4[sp]
        st.w    r1,  8[sp]
        /* R10 has already been saved before callt ctoff(_save_interrupt).  */
        /* st.w    r10, 12[sp]  */
	mov	hilo(__ep),ep
	mov	hilo(__gp),gp
	ctret

	/* Restore saved registers, deallocate stack and return from the interrupt.  */
        /* Called via:  callt ctoff(__callt_restore_interrupt).  */
	.align	2
	.globl	__return_interrupt
	.type	__return_interrupt,@function
.L_return_interrupt:
        ld.w    20[sp], r1
        ldsr    r1,     ctpsw
        ld.w    16[sp], r1
        ldsr    r1,     ctpc
        ld.w    12[sp], r10
        ld.w     8[sp], r1
        ld.w     4[sp], gp
        ld.w     0[sp], ep
        addi    24, sp, sp
        reti

	/* Place the offsets of the start of these routines into the call table.  */
	.call_table_data

        .global __callt_save_interrupt
        .type   __callt_save_interrupt,@function
__callt_save_interrupt:         .short ctoff(.L_save_interrupt)

        .global __callt_return_interrupt
        .type   __callt_return_interrupt,@function
__callt_return_interrupt:       .short ctoff(.L_return_interrupt)
	
#endif /* L_callt_save_interrupt */

#ifdef L_callt_save_all_interrupt
	/* Put these functions into the call table area.  */
	.call_table_text
	
	/* Save all registers except for those saved in __save_interrupt.  */
	/* Allocate enough stack for all of the registers & 16 bytes of space.  */
	/* Called via:	callt ctoff(__callt_save_all_interrupt).  */
	.align	2
.L_save_all_interrupt:
	addi	-60, sp, sp
#ifdef __EP__
	mov	ep,  r1
	mov	sp,  ep
	sst.w	r2,  56[ep]
	sst.w	r5,  52[ep]
	sst.w	r6,  48[ep]
	sst.w	r7,  44[ep]
	sst.w	r8,  40[ep]
	sst.w	r9,  36[ep]
	sst.w	r11, 32[ep]
	sst.w	r12, 28[ep]
	sst.w	r13, 24[ep]
	sst.w	r14, 20[ep]
	sst.w	r15, 16[ep]
	sst.w	r16, 12[ep]
	sst.w	r17, 8[ep]
	sst.w	r18, 4[ep]
	sst.w	r19, 0[ep]
	mov	r1,  ep
#else
	st.w	r2,  56[sp]
	st.w	r5,  52[sp]
	st.w	r6,  48[sp]
	st.w	r7,  44[sp]
	st.w	r8,  40[sp]
	st.w	r9,  36[sp]
	st.w	r11, 32[sp]
	st.w	r12, 28[sp]
	st.w	r13, 24[sp]
	st.w	r14, 20[sp]
	st.w	r15, 16[sp]
	st.w	r16, 12[sp]
	st.w	r17, 8[sp]
	st.w	r18, 4[sp]
	st.w	r19, 0[sp]
#endif
	prepare {r20 - r29, r31}, 4
	ctret	

	/* Restore all registers saved in __save_all_interrupt
	   deallocate the stack space.  */
	/* Called via:	callt ctoff(__callt_restore_all_interrupt).  */
	.align 2
.L_restore_all_interrupt:
	dispose 4, {r20 - r29, r31}
#ifdef __EP__	
	mov	ep, r1
	mov	sp, ep
	sld.w	0 [ep], r19
	sld.w	4 [ep], r18
	sld.w	8 [ep], r17
	sld.w	12[ep], r16
	sld.w	16[ep], r15
	sld.w	20[ep], r14
	sld.w	24[ep], r13
	sld.w	28[ep], r12
	sld.w	32[ep], r11
	sld.w	36[ep], r9
	sld.w	40[ep], r8
	sld.w	44[ep], r7
	sld.w	48[ep], r6
	sld.w	52[ep], r5
	sld.w	56[ep], r2
	mov	r1, ep
#else
	ld.w	0 [sp], r19
	ld.w	4 [sp], r18
	ld.w	8 [sp], r17
	ld.w	12[sp], r16
	ld.w	16[sp], r15
	ld.w	20[sp], r14
	ld.w	24[sp], r13
	ld.w	28[sp], r12
	ld.w	32[sp], r11
	ld.w	36[sp], r9
	ld.w	40[sp], r8
	ld.w	44[sp], r7
	ld.w	48[sp], r6
	ld.w	52[sp], r5
	ld.w	56[sp], r2
#endif
	addi	60, sp, sp
	ctret

	/* Place the offsets of the start of these routines into the call table.  */
	.call_table_data

	.global	__callt_save_all_interrupt
	.type	__callt_save_all_interrupt,@function
__callt_save_all_interrupt:	.short ctoff(.L_save_all_interrupt)
	
	.global	__callt_restore_all_interrupt
	.type	__callt_restore_all_interrupt,@function
__callt_restore_all_interrupt:	.short ctoff(.L_restore_all_interrupt)
	
#endif /* L_callt_save_all_interrupt */


#define MAKE_CALLT_FUNCS( START )						\
	.call_table_text							;\
	.align	2								;\
	/* Allocate space and save registers START .. r29 on the stack.  */	;\
	/* Called via:	callt ctoff(__callt_save_START_r29).  */		;\
.L_save_##START##_r29:								;\
	prepare { START - r29 }, 0						;\
	ctret									;\
										;\
	/* Restore saved registers, deallocate stack and return.  */		;\
	/* Called via:	callt ctoff(__return_START_r29) */			;\
	.align	2								;\
.L_return_##START##_r29:							;\
	dispose 0, { START - r29 }, r31						;\
										;\
	/* Place the offsets of the start of these funcs into the call table.  */;\
	.call_table_data							;\
										;\
	.global	__callt_save_##START##_r29					;\
	.type	__callt_save_##START##_r29,@function				;\
__callt_save_##START##_r29:	.short ctoff(.L_save_##START##_r29 )		;\
										;\
	.global	__callt_return_##START##_r29					;\
	.type	__callt_return_##START##_r29,@function				;\
__callt_return_##START##_r29:	.short ctoff(.L_return_##START##_r29 )	


#define MAKE_CALLT_CFUNCS( START )						\
	.call_table_text							;\
	.align	2								;\
	/* Allocate space and save registers START .. r31 on the stack.  */	;\
	/* Called via:	callt ctoff(__callt_save_START_r31c).  */		;\
.L_save_##START##_r31c:								;\
	prepare { START - r29, r31}, 4						;\
	ctret									;\
										;\
	/* Restore saved registers, deallocate stack and return.  */		;\
	/* Called via:	callt ctoff(__return_START_r31c).  */			;\
	.align	2								;\
.L_return_##START##_r31c:							;\
	dispose 4, { START - r29, r31}, r31					;\
										;\
	/* Place the offsets of the start of these funcs into the call table.  */;\
	.call_table_data							;\
										;\
	.global	__callt_save_##START##_r31c					;\
	.type	__callt_save_##START##_r31c,@function				;\
__callt_save_##START##_r31c:    .short ctoff(.L_save_##START##_r31c )		;\
										;\
	.global	__callt_return_##START##_r31c					;\
	.type	__callt_return_##START##_r31c,@function				;\
__callt_return_##START##_r31c:  .short ctoff(.L_return_##START##_r31c )	

	
#ifdef	L_callt_save_20
	MAKE_CALLT_FUNCS (r20)
#endif
#ifdef	L_callt_save_21
	MAKE_CALLT_FUNCS (r21)
#endif
#ifdef	L_callt_save_22
	MAKE_CALLT_FUNCS (r22)
#endif
#ifdef	L_callt_save_23
	MAKE_CALLT_FUNCS (r23)
#endif
#ifdef	L_callt_save_24
	MAKE_CALLT_FUNCS (r24)
#endif
#ifdef	L_callt_save_25
	MAKE_CALLT_FUNCS (r25)
#endif
#ifdef	L_callt_save_26
	MAKE_CALLT_FUNCS (r26)
#endif
#ifdef	L_callt_save_27
	MAKE_CALLT_FUNCS (r27)
#endif
#ifdef	L_callt_save_28
	MAKE_CALLT_FUNCS (r28)
#endif
#ifdef	L_callt_save_29
	MAKE_CALLT_FUNCS (r29)
#endif

#ifdef	L_callt_save_20c
	MAKE_CALLT_CFUNCS (r20)
#endif
#ifdef	L_callt_save_21c
	MAKE_CALLT_CFUNCS (r21)
#endif
#ifdef	L_callt_save_22c
	MAKE_CALLT_CFUNCS (r22)
#endif
#ifdef	L_callt_save_23c
	MAKE_CALLT_CFUNCS (r23)
#endif
#ifdef	L_callt_save_24c
	MAKE_CALLT_CFUNCS (r24)
#endif
#ifdef	L_callt_save_25c
	MAKE_CALLT_CFUNCS (r25)
#endif
#ifdef	L_callt_save_26c
	MAKE_CALLT_CFUNCS (r26)
#endif
#ifdef	L_callt_save_27c
	MAKE_CALLT_CFUNCS (r27)
#endif
#ifdef	L_callt_save_28c
	MAKE_CALLT_CFUNCS (r28)
#endif
#ifdef	L_callt_save_29c
	MAKE_CALLT_CFUNCS (r29)
#endif

	
#ifdef	L_callt_save_31c
	.call_table_text
	.align	2
	/* Allocate space and save register r31 on the stack.  */
	/* Called via:	callt ctoff(__callt_save_r31c).  */
.L_callt_save_r31c:
	prepare {r31}, 4
	ctret

	/* Restore saved registers, deallocate stack and return.  */
	/* Called via:	callt ctoff(__return_r31c).  */
	.align	2
.L_callt_return_r31c:
	dispose 4, {r31}, r31
	
	/* Place the offsets of the start of these funcs into the call table.  */
	.call_table_data

	.global	__callt_save_r31c
	.type	__callt_save_r31c,@function
__callt_save_r31c:	.short ctoff(.L_callt_save_r31c)

	.global	__callt_return_r31c
	.type	__callt_return_r31c,@function
__callt_return_r31c:	.short ctoff(.L_callt_return_r31c)		
#endif

#endif /* __v850e__ */

/*  libgcc2 routines for NEC V850.  */
/*  Double Integer Arithmetical Operation.  */

#ifdef L_negdi2
	.text
	.global ___negdi2
	.type   ___negdi2, @function
___negdi2:
	not	r6, r10
	add	1,  r10
	setf	l,  r6
	not	r7, r11
	add	r6, r11
	jmp	[lp]

	.size ___negdi2,.-___negdi2
#endif

#ifdef L_cmpdi2
	.text
	.global ___cmpdi2
	.type	___cmpdi2,@function
___cmpdi2:
	# Signed comparison bitween each high word.
	cmp	r9, r7
	be	.L_cmpdi_cmp_low
	setf	ge, r10
	setf	gt, r6
	add	r6, r10
	jmp	[lp]
.L_cmpdi_cmp_low:
	# Unsigned comparigon bitween each low word.
	cmp     r8, r6
	setf	nl, r10
	setf	h,  r6
	add	r6, r10
	jmp	[lp]	
	.size ___cmpdi2, . - ___cmpdi2	
#endif

#ifdef L_ucmpdi2
	.text
	.global ___ucmpdi2
	.type	___ucmpdi2,@function
___ucmpdi2:
	cmp	r9, r7  # Check if each high word are same.
	bne	.L_ucmpdi_check_psw
	cmp     r8, r6  # Compare the word.
.L_ucmpdi_check_psw:
	setf	nl, r10 # 
	setf	h,  r6  # 
	add	r6, r10 # Add the result of comparison NL and comparison H.
	jmp	[lp]	
	.size ___ucmpdi2, . - ___ucmpdi2
#endif

#ifdef L_muldi3
	.text
	.global ___muldi3
	.type	___muldi3,@function
___muldi3:
#ifdef __v850__
        jarl  __save_r26_r31, r10
        addi  16,  sp, sp
        mov   r6,  r28
        shr   15,  r28
        movea lo(32767), r0, r14
        and   r14, r28
        mov   r8,  r10
        shr   15,  r10
        and   r14, r10
        mov   r6,  r19
        shr   30,  r19
        mov   r7,  r12
        shl   2,   r12
        or    r12, r19
        and   r14, r19
        mov   r8,  r13
        shr   30,  r13
        mov   r9,  r12
        shl   2,   r12
        or    r12, r13
        and   r14, r13
        mov   r7,  r11
        shr   13,  r11
        and   r14, r11
        mov   r9,  r31
        shr   13,  r31
        and   r14, r31
        mov   r7,  r29
        shr   28,  r29
        and   r14, r29
        mov   r9,  r12
        shr   28,  r12
        and   r14, r12
        and   r14, r6
        and   r14, r8
        mov   r6,  r14
        mulh  r8,  r14
        mov   r6,  r16
        mulh  r10, r16
        mov   r6,  r18
        mulh  r13, r18
        mov   r6,  r15
        mulh  r31, r15
        mulh  r12, r6
        mov   r28,  r17
        mulh  r10, r17
        add   -16, sp
        mov   r28,  r12
        mulh  r8,  r12
        add   r17, r18
        mov   r28,  r17
        mulh  r31, r17
        add   r12, r16
        mov   r28,  r12
        mulh  r13, r12
        add   r17, r6
        mov   r19, r17
        add   r12, r15
        mov   r19, r12
        mulh  r8,  r12
        mulh  r10, r17
        add   r12, r18
        mov   r19, r12
        mulh  r13, r12
        add   r17, r15
        mov   r11, r13
        mulh  r8,  r13
        add   r12, r6
        mov   r11, r12
        mulh  r10, r12
        add   r13, r15
        mulh  r29, r8
        add   r12, r6
        mov   r16, r13
        shl   15,  r13
        add   r14, r13
        mov   r18, r12
        shl   30,  r12
        mov   r13, r26
        add   r12, r26
        shr   15,  r14
        movhi hi(131071), r0,  r12
        movea lo(131071), r12, r13
        and   r13, r14
        mov   r16, r12
        and   r13, r12
        add   r12, r14
        mov   r18, r12
        shl   15,  r12
        and   r13, r12
        add   r12, r14
        shr   17,  r14
        shr   17,  r16
        add   r14, r16
        shl   13,  r15
        shr   2,   r18
        add   r18, r15
        add   r15, r16
        mov   r16, r27
        add   r8,  r6
        shl   28,  r6
        add   r6,  r27
        mov   r26, r10
        mov   r27, r11
        jr    __return_r26_r31
#endif /* __v850__ */
#if defined(__v850e__) || defined(__v850ea__)
	/*  (Ahi << 32 + Alo) * (Bhi << 32 + Blo) */
	/*   r7           r6      r9         r8   */
	mov  r8, r10
	mulu r7, r8,  r0		/* Ahi * Blo */
	mulu r6, r9,  r0		/* Alo * Bhi */
	mulu r6, r10, r11		/* Alo * Blo */
	add  r8, r11
	add  r9, r11
	jmp  [r31]

#endif /* defined(__v850e__)  || defined(__v850ea__) */
	.size ___muldi3, . - ___muldi3
#endif
