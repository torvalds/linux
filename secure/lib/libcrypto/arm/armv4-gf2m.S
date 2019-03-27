/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from armv4-gf2m.pl. */
#include "arm_arch.h"

.text
#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif
.type	mul_1x1_ialu,%function
.align	5
mul_1x1_ialu:
	mov	r4,#0
	bic	r5,r1,#3<<30		@ a1=a&0x3fffffff
	str	r4,[sp,#0]		@ tab[0]=0
	add	r6,r5,r5		@ a2=a1<<1
	str	r5,[sp,#4]		@ tab[1]=a1
	eor	r7,r5,r6		@ a1^a2
	str	r6,[sp,#8]		@ tab[2]=a2
	mov	r8,r5,lsl#2		@ a4=a1<<2
	str	r7,[sp,#12]		@ tab[3]=a1^a2
	eor	r9,r5,r8		@ a1^a4
	str	r8,[sp,#16]		@ tab[4]=a4
	eor	r4,r6,r8		@ a2^a4
	str	r9,[sp,#20]		@ tab[5]=a1^a4
	eor	r7,r7,r8		@ a1^a2^a4
	str	r4,[sp,#24]		@ tab[6]=a2^a4
	and	r8,r12,r0,lsl#2
	str	r7,[sp,#28]		@ tab[7]=a1^a2^a4

	and	r9,r12,r0,lsr#1
	ldr	r5,[sp,r8]		@ tab[b       & 0x7]
	and	r8,r12,r0,lsr#4
	ldr	r7,[sp,r9]		@ tab[b >>  3 & 0x7]
	and	r9,r12,r0,lsr#7
	ldr	r6,[sp,r8]		@ tab[b >>  6 & 0x7]
	eor	r5,r5,r7,lsl#3	@ stall
	mov	r4,r7,lsr#29
	ldr	r7,[sp,r9]		@ tab[b >>  9 & 0x7]

	and	r8,r12,r0,lsr#10
	eor	r5,r5,r6,lsl#6
	eor	r4,r4,r6,lsr#26
	ldr	r6,[sp,r8]		@ tab[b >> 12 & 0x7]

	and	r9,r12,r0,lsr#13
	eor	r5,r5,r7,lsl#9
	eor	r4,r4,r7,lsr#23
	ldr	r7,[sp,r9]		@ tab[b >> 15 & 0x7]

	and	r8,r12,r0,lsr#16
	eor	r5,r5,r6,lsl#12
	eor	r4,r4,r6,lsr#20
	ldr	r6,[sp,r8]		@ tab[b >> 18 & 0x7]

	and	r9,r12,r0,lsr#19
	eor	r5,r5,r7,lsl#15
	eor	r4,r4,r7,lsr#17
	ldr	r7,[sp,r9]		@ tab[b >> 21 & 0x7]

	and	r8,r12,r0,lsr#22
	eor	r5,r5,r6,lsl#18
	eor	r4,r4,r6,lsr#14
	ldr	r6,[sp,r8]		@ tab[b >> 24 & 0x7]

	and	r9,r12,r0,lsr#25
	eor	r5,r5,r7,lsl#21
	eor	r4,r4,r7,lsr#11
	ldr	r7,[sp,r9]		@ tab[b >> 27 & 0x7]

	tst	r1,#1<<30
	and	r8,r12,r0,lsr#28
	eor	r5,r5,r6,lsl#24
	eor	r4,r4,r6,lsr#8
	ldr	r6,[sp,r8]		@ tab[b >> 30      ]

#ifdef	__thumb2__
	itt	ne
#endif
	eorne	r5,r5,r0,lsl#30
	eorne	r4,r4,r0,lsr#2
	tst	r1,#1<<31
	eor	r5,r5,r7,lsl#27
	eor	r4,r4,r7,lsr#5
#ifdef	__thumb2__
	itt	ne
#endif
	eorne	r5,r5,r0,lsl#31
	eorne	r4,r4,r0,lsr#1
	eor	r5,r5,r6,lsl#30
	eor	r4,r4,r6,lsr#2

	mov	pc,lr
.size	mul_1x1_ialu,.-mul_1x1_ialu
.globl	bn_GF2m_mul_2x2
.type	bn_GF2m_mul_2x2,%function
.align	5
bn_GF2m_mul_2x2:
#if __ARM_MAX_ARCH__>=7
	stmdb	sp!,{r10,lr}
	ldr	r12,.LOPENSSL_armcap
	adr	r10,.LOPENSSL_armcap
	ldr	r12,[r12,r10]
#ifdef	__APPLE__
	ldr	r12,[r12]
#endif
	tst	r12,#ARMV7_NEON
	itt	ne
	ldrne	r10,[sp],#8
	bne	.LNEON
	stmdb	sp!,{r4,r5,r6,r7,r8,r9}
#else
	stmdb	sp!,{r4,r5,r6,r7,r8,r9,r10,lr}
#endif
	mov	r10,r0			@ reassign 1st argument
	mov	r0,r3			@ r0=b1
	sub	r7,sp,#36
	mov	r8,sp
	and	r7,r7,#-32
	ldr	r3,[sp,#32]		@ load b0
	mov	r12,#7<<2
	mov	sp,r7			@ allocate tab[8]
	str	r8,[r7,#32]

	bl	mul_1x1_ialu		@ a1·b1
	str	r5,[r10,#8]
	str	r4,[r10,#12]

	eor	r0,r0,r3		@ flip b0 and b1
	eor	r1,r1,r2		@ flip a0 and a1
	eor	r3,r3,r0
	eor	r2,r2,r1
	eor	r0,r0,r3
	eor	r1,r1,r2
	bl	mul_1x1_ialu		@ a0·b0
	str	r5,[r10]
	str	r4,[r10,#4]

	eor	r1,r1,r2
	eor	r0,r0,r3
	bl	mul_1x1_ialu		@ (a1+a0)·(b1+b0)
	ldmia	r10,{r6,r7,r8,r9}
	eor	r5,r5,r4
	ldr	sp,[sp,#32]		@ destroy tab[8]
	eor	r4,r4,r7
	eor	r5,r5,r6
	eor	r4,r4,r8
	eor	r5,r5,r9
	eor	r4,r4,r9
	str	r4,[r10,#8]
	eor	r5,r5,r4
	str	r5,[r10,#4]

#if __ARM_ARCH__>=5
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,pc}
#else
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.align	5
.LNEON:
	ldr	r12, [sp]		@ 5th argument
	vmov	d26, r2, r1
	vmov	d27, r12, r3
	vmov.i64	d28, #0x0000ffffffffffff
	vmov.i64	d29, #0x00000000ffffffff
	vmov.i64	d30, #0x000000000000ffff

	vext.8	d2, d26, d26, #1	@ A1
	vmull.p8	q1, d2, d27		@ F = A1*B
	vext.8	d0, d27, d27, #1	@ B1
	vmull.p8	q0, d26, d0		@ E = A*B1
	vext.8	d4, d26, d26, #2	@ A2
	vmull.p8	q2, d4, d27		@ H = A2*B
	vext.8	d16, d27, d27, #2	@ B2
	vmull.p8	q8, d26, d16		@ G = A*B2
	vext.8	d6, d26, d26, #3	@ A3
	veor	q1, q1, q0		@ L = E + F
	vmull.p8	q3, d6, d27		@ J = A3*B
	vext.8	d0, d27, d27, #3	@ B3
	veor	q2, q2, q8		@ M = G + H
	vmull.p8	q0, d26, d0		@ I = A*B3
	veor	d2, d2, d3	@ t0 = (L) (P0 + P1) << 8
	vand	d3, d3, d28
	vext.8	d16, d27, d27, #4	@ B4
	veor	d4, d4, d5	@ t1 = (M) (P2 + P3) << 16
	vand	d5, d5, d29
	vmull.p8	q8, d26, d16		@ K = A*B4
	veor	q3, q3, q0		@ N = I + J
	veor	d2, d2, d3
	veor	d4, d4, d5
	veor	d6, d6, d7	@ t2 = (N) (P4 + P5) << 24
	vand	d7, d7, d30
	vext.8	q1, q1, q1, #15
	veor	d16, d16, d17	@ t3 = (K) (P6 + P7) << 32
	vmov.i64	d17, #0
	vext.8	q2, q2, q2, #14
	veor	d6, d6, d7
	vmull.p8	q0, d26, d27		@ D = A*B
	vext.8	q8, q8, q8, #12
	vext.8	q3, q3, q3, #13
	veor	q1, q1, q2
	veor	q3, q3, q8
	veor	q0, q0, q1
	veor	q0, q0, q3

	vst1.32	{q0}, [r0]
	bx	lr		@ bx lr
#endif
.size	bn_GF2m_mul_2x2,.-bn_GF2m_mul_2x2
#if __ARM_MAX_ARCH__>=7
.align	5
.LOPENSSL_armcap:
.word	OPENSSL_armcap_P-.
#endif
.byte	71,70,40,50,94,109,41,32,77,117,108,116,105,112,108,105,99,97,116,105,111,110,32,102,111,114,32,65,82,77,118,52,47,78,69,79,78,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	5

#if __ARM_MAX_ARCH__>=7
.comm	OPENSSL_armcap_P,4,4
#endif
