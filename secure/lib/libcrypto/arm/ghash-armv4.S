/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from ghash-armv4.pl. */
#include "arm_arch.h"

.text
#if defined(__thumb2__) || defined(__clang__)
.syntax	unified
#define ldrplb  ldrbpl
#define ldrneb  ldrbne
#endif
#if defined(__thumb2__)
.thumb
#else
.code	32
#endif

.type	rem_4bit,%object
.align	5
rem_4bit:
.short	0x0000,0x1C20,0x3840,0x2460
.short	0x7080,0x6CA0,0x48C0,0x54E0
.short	0xE100,0xFD20,0xD940,0xC560
.short	0x9180,0x8DA0,0xA9C0,0xB5E0
.size	rem_4bit,.-rem_4bit

.type	rem_4bit_get,%function
rem_4bit_get:
#if defined(__thumb2__)
	adr	r2,rem_4bit
#else
	sub	r2,pc,#8+32	@ &rem_4bit
#endif
	b	.Lrem_4bit_got
	nop
	nop
.size	rem_4bit_get,.-rem_4bit_get

.globl	gcm_ghash_4bit
.type	gcm_ghash_4bit,%function
.align	4
gcm_ghash_4bit:
#if defined(__thumb2__)
	adr	r12,rem_4bit
#else
	sub	r12,pc,#8+48		@ &rem_4bit
#endif
	add	r3,r2,r3		@ r3 to point at the end
	stmdb	sp!,{r3,r4,r5,r6,r7,r8,r9,r10,r11,lr}		@ save r3/end too

	ldmia	r12,{r4,r5,r6,r7,r8,r9,r10,r11}		@ copy rem_4bit ...
	stmdb	sp!,{r4,r5,r6,r7,r8,r9,r10,r11}		@ ... to stack

	ldrb	r12,[r2,#15]
	ldrb	r14,[r0,#15]
.Louter:
	eor	r12,r12,r14
	and	r14,r12,#0xf0
	and	r12,r12,#0x0f
	mov	r3,#14

	add	r7,r1,r12,lsl#4
	ldmia	r7,{r4,r5,r6,r7}	@ load Htbl[nlo]
	add	r11,r1,r14
	ldrb	r12,[r2,#14]

	and	r14,r4,#0xf		@ rem
	ldmia	r11,{r8,r9,r10,r11}	@ load Htbl[nhi]
	add	r14,r14,r14
	eor	r4,r8,r4,lsr#4
	ldrh	r8,[sp,r14]		@ rem_4bit[rem]
	eor	r4,r4,r5,lsl#28
	ldrb	r14,[r0,#14]
	eor	r5,r9,r5,lsr#4
	eor	r5,r5,r6,lsl#28
	eor	r6,r10,r6,lsr#4
	eor	r6,r6,r7,lsl#28
	eor	r7,r11,r7,lsr#4
	eor	r12,r12,r14
	and	r14,r12,#0xf0
	and	r12,r12,#0x0f
	eor	r7,r7,r8,lsl#16

.Linner:
	add	r11,r1,r12,lsl#4
	and	r12,r4,#0xf		@ rem
	subs	r3,r3,#1
	add	r12,r12,r12
	ldmia	r11,{r8,r9,r10,r11}	@ load Htbl[nlo]
	eor	r4,r8,r4,lsr#4
	eor	r4,r4,r5,lsl#28
	eor	r5,r9,r5,lsr#4
	eor	r5,r5,r6,lsl#28
	ldrh	r8,[sp,r12]		@ rem_4bit[rem]
	eor	r6,r10,r6,lsr#4
#ifdef	__thumb2__
	it	pl
#endif
	ldrplb	r12,[r2,r3]
	eor	r6,r6,r7,lsl#28
	eor	r7,r11,r7,lsr#4

	add	r11,r1,r14
	and	r14,r4,#0xf		@ rem
	eor	r7,r7,r8,lsl#16	@ ^= rem_4bit[rem]
	add	r14,r14,r14
	ldmia	r11,{r8,r9,r10,r11}	@ load Htbl[nhi]
	eor	r4,r8,r4,lsr#4
#ifdef	__thumb2__
	it	pl
#endif
	ldrplb	r8,[r0,r3]
	eor	r4,r4,r5,lsl#28
	eor	r5,r9,r5,lsr#4
	ldrh	r9,[sp,r14]
	eor	r5,r5,r6,lsl#28
	eor	r6,r10,r6,lsr#4
	eor	r6,r6,r7,lsl#28
#ifdef	__thumb2__
	it	pl
#endif
	eorpl	r12,r12,r8
	eor	r7,r11,r7,lsr#4
#ifdef	__thumb2__
	itt	pl
#endif
	andpl	r14,r12,#0xf0
	andpl	r12,r12,#0x0f
	eor	r7,r7,r9,lsl#16	@ ^= rem_4bit[rem]
	bpl	.Linner

	ldr	r3,[sp,#32]		@ re-load r3/end
	add	r2,r2,#16
	mov	r14,r4
#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	r4,r4
	str	r4,[r0,#12]
#elif defined(__ARMEB__)
	str	r4,[r0,#12]
#else
	mov	r9,r4,lsr#8
	strb	r4,[r0,#12+3]
	mov	r10,r4,lsr#16
	strb	r9,[r0,#12+2]
	mov	r11,r4,lsr#24
	strb	r10,[r0,#12+1]
	strb	r11,[r0,#12]
#endif
	cmp	r2,r3
#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	r5,r5
	str	r5,[r0,#8]
#elif defined(__ARMEB__)
	str	r5,[r0,#8]
#else
	mov	r9,r5,lsr#8
	strb	r5,[r0,#8+3]
	mov	r10,r5,lsr#16
	strb	r9,[r0,#8+2]
	mov	r11,r5,lsr#24
	strb	r10,[r0,#8+1]
	strb	r11,[r0,#8]
#endif

#ifdef __thumb2__
	it	ne
#endif
	ldrneb	r12,[r2,#15]
#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	r6,r6
	str	r6,[r0,#4]
#elif defined(__ARMEB__)
	str	r6,[r0,#4]
#else
	mov	r9,r6,lsr#8
	strb	r6,[r0,#4+3]
	mov	r10,r6,lsr#16
	strb	r9,[r0,#4+2]
	mov	r11,r6,lsr#24
	strb	r10,[r0,#4+1]
	strb	r11,[r0,#4]
#endif

#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	r7,r7
	str	r7,[r0,#0]
#elif defined(__ARMEB__)
	str	r7,[r0,#0]
#else
	mov	r9,r7,lsr#8
	strb	r7,[r0,#0+3]
	mov	r10,r7,lsr#16
	strb	r9,[r0,#0+2]
	mov	r11,r7,lsr#24
	strb	r10,[r0,#0+1]
	strb	r11,[r0,#0]
#endif

	bne	.Louter

	add	sp,sp,#36
#if __ARM_ARCH__>=5
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,pc}
#else
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
.size	gcm_ghash_4bit,.-gcm_ghash_4bit

.globl	gcm_gmult_4bit
.type	gcm_gmult_4bit,%function
gcm_gmult_4bit:
	stmdb	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,lr}
	ldrb	r12,[r0,#15]
	b	rem_4bit_get
.Lrem_4bit_got:
	and	r14,r12,#0xf0
	and	r12,r12,#0x0f
	mov	r3,#14

	add	r7,r1,r12,lsl#4
	ldmia	r7,{r4,r5,r6,r7}	@ load Htbl[nlo]
	ldrb	r12,[r0,#14]

	add	r11,r1,r14
	and	r14,r4,#0xf		@ rem
	ldmia	r11,{r8,r9,r10,r11}	@ load Htbl[nhi]
	add	r14,r14,r14
	eor	r4,r8,r4,lsr#4
	ldrh	r8,[r2,r14]	@ rem_4bit[rem]
	eor	r4,r4,r5,lsl#28
	eor	r5,r9,r5,lsr#4
	eor	r5,r5,r6,lsl#28
	eor	r6,r10,r6,lsr#4
	eor	r6,r6,r7,lsl#28
	eor	r7,r11,r7,lsr#4
	and	r14,r12,#0xf0
	eor	r7,r7,r8,lsl#16
	and	r12,r12,#0x0f

.Loop:
	add	r11,r1,r12,lsl#4
	and	r12,r4,#0xf		@ rem
	subs	r3,r3,#1
	add	r12,r12,r12
	ldmia	r11,{r8,r9,r10,r11}	@ load Htbl[nlo]
	eor	r4,r8,r4,lsr#4
	eor	r4,r4,r5,lsl#28
	eor	r5,r9,r5,lsr#4
	eor	r5,r5,r6,lsl#28
	ldrh	r8,[r2,r12]	@ rem_4bit[rem]
	eor	r6,r10,r6,lsr#4
#ifdef	__thumb2__
	it	pl
#endif
	ldrplb	r12,[r0,r3]
	eor	r6,r6,r7,lsl#28
	eor	r7,r11,r7,lsr#4

	add	r11,r1,r14
	and	r14,r4,#0xf		@ rem
	eor	r7,r7,r8,lsl#16	@ ^= rem_4bit[rem]
	add	r14,r14,r14
	ldmia	r11,{r8,r9,r10,r11}	@ load Htbl[nhi]
	eor	r4,r8,r4,lsr#4
	eor	r4,r4,r5,lsl#28
	eor	r5,r9,r5,lsr#4
	ldrh	r8,[r2,r14]	@ rem_4bit[rem]
	eor	r5,r5,r6,lsl#28
	eor	r6,r10,r6,lsr#4
	eor	r6,r6,r7,lsl#28
	eor	r7,r11,r7,lsr#4
#ifdef	__thumb2__
	itt	pl
#endif
	andpl	r14,r12,#0xf0
	andpl	r12,r12,#0x0f
	eor	r7,r7,r8,lsl#16	@ ^= rem_4bit[rem]
	bpl	.Loop
#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	r4,r4
	str	r4,[r0,#12]
#elif defined(__ARMEB__)
	str	r4,[r0,#12]
#else
	mov	r9,r4,lsr#8
	strb	r4,[r0,#12+3]
	mov	r10,r4,lsr#16
	strb	r9,[r0,#12+2]
	mov	r11,r4,lsr#24
	strb	r10,[r0,#12+1]
	strb	r11,[r0,#12]
#endif

#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	r5,r5
	str	r5,[r0,#8]
#elif defined(__ARMEB__)
	str	r5,[r0,#8]
#else
	mov	r9,r5,lsr#8
	strb	r5,[r0,#8+3]
	mov	r10,r5,lsr#16
	strb	r9,[r0,#8+2]
	mov	r11,r5,lsr#24
	strb	r10,[r0,#8+1]
	strb	r11,[r0,#8]
#endif

#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	r6,r6
	str	r6,[r0,#4]
#elif defined(__ARMEB__)
	str	r6,[r0,#4]
#else
	mov	r9,r6,lsr#8
	strb	r6,[r0,#4+3]
	mov	r10,r6,lsr#16
	strb	r9,[r0,#4+2]
	mov	r11,r6,lsr#24
	strb	r10,[r0,#4+1]
	strb	r11,[r0,#4]
#endif

#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	r7,r7
	str	r7,[r0,#0]
#elif defined(__ARMEB__)
	str	r7,[r0,#0]
#else
	mov	r9,r7,lsr#8
	strb	r7,[r0,#0+3]
	mov	r10,r7,lsr#16
	strb	r9,[r0,#0+2]
	mov	r11,r7,lsr#24
	strb	r10,[r0,#0+1]
	strb	r11,[r0,#0]
#endif

#if __ARM_ARCH__>=5
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,pc}
#else
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
.size	gcm_gmult_4bit,.-gcm_gmult_4bit
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.globl	gcm_init_neon
.type	gcm_init_neon,%function
.align	4
gcm_init_neon:
	vld1.64	d7,[r1]!		@ load H
	vmov.i8	q8,#0xe1
	vld1.64	d6,[r1]
	vshl.i64	d17,#57
	vshr.u64	d16,#63		@ t0=0xc2....01
	vdup.8	q9,d7[7]
	vshr.u64	d26,d6,#63
	vshr.s8	q9,#7			@ broadcast carry bit
	vshl.i64	q3,q3,#1
	vand	q8,q8,q9
	vorr	d7,d26		@ H<<<=1
	veor	q3,q3,q8		@ twisted H
	vstmia	r0,{q3}

	bx	lr					@ bx lr
.size	gcm_init_neon,.-gcm_init_neon

.globl	gcm_gmult_neon
.type	gcm_gmult_neon,%function
.align	4
gcm_gmult_neon:
	vld1.64	d7,[r0]!		@ load Xi
	vld1.64	d6,[r0]!
	vmov.i64	d29,#0x0000ffffffffffff
	vldmia	r1,{d26,d27}	@ load twisted H
	vmov.i64	d30,#0x00000000ffffffff
#ifdef __ARMEL__
	vrev64.8	q3,q3
#endif
	vmov.i64	d31,#0x000000000000ffff
	veor	d28,d26,d27		@ Karatsuba pre-processing
	mov	r3,#16
	b	.Lgmult_neon
.size	gcm_gmult_neon,.-gcm_gmult_neon

.globl	gcm_ghash_neon
.type	gcm_ghash_neon,%function
.align	4
gcm_ghash_neon:
	vld1.64	d1,[r0]!		@ load Xi
	vld1.64	d0,[r0]!
	vmov.i64	d29,#0x0000ffffffffffff
	vldmia	r1,{d26,d27}	@ load twisted H
	vmov.i64	d30,#0x00000000ffffffff
#ifdef __ARMEL__
	vrev64.8	q0,q0
#endif
	vmov.i64	d31,#0x000000000000ffff
	veor	d28,d26,d27		@ Karatsuba pre-processing

.Loop_neon:
	vld1.64	d7,[r2]!		@ load inp
	vld1.64	d6,[r2]!
#ifdef __ARMEL__
	vrev64.8	q3,q3
#endif
	veor	q3,q0			@ inp^=Xi
.Lgmult_neon:
	vext.8	d16, d26, d26, #1	@ A1
	vmull.p8	q8, d16, d6		@ F = A1*B
	vext.8	d0, d6, d6, #1	@ B1
	vmull.p8	q0, d26, d0		@ E = A*B1
	vext.8	d18, d26, d26, #2	@ A2
	vmull.p8	q9, d18, d6		@ H = A2*B
	vext.8	d22, d6, d6, #2	@ B2
	vmull.p8	q11, d26, d22		@ G = A*B2
	vext.8	d20, d26, d26, #3	@ A3
	veor	q8, q8, q0		@ L = E + F
	vmull.p8	q10, d20, d6		@ J = A3*B
	vext.8	d0, d6, d6, #3	@ B3
	veor	q9, q9, q11		@ M = G + H
	vmull.p8	q0, d26, d0		@ I = A*B3
	veor	d16, d16, d17	@ t0 = (L) (P0 + P1) << 8
	vand	d17, d17, d29
	vext.8	d22, d6, d6, #4	@ B4
	veor	d18, d18, d19	@ t1 = (M) (P2 + P3) << 16
	vand	d19, d19, d30
	vmull.p8	q11, d26, d22		@ K = A*B4
	veor	q10, q10, q0		@ N = I + J
	veor	d16, d16, d17
	veor	d18, d18, d19
	veor	d20, d20, d21	@ t2 = (N) (P4 + P5) << 24
	vand	d21, d21, d31
	vext.8	q8, q8, q8, #15
	veor	d22, d22, d23	@ t3 = (K) (P6 + P7) << 32
	vmov.i64	d23, #0
	vext.8	q9, q9, q9, #14
	veor	d20, d20, d21
	vmull.p8	q0, d26, d6		@ D = A*B
	vext.8	q11, q11, q11, #12
	vext.8	q10, q10, q10, #13
	veor	q8, q8, q9
	veor	q10, q10, q11
	veor	q0, q0, q8
	veor	q0, q0, q10
	veor	d6,d6,d7	@ Karatsuba pre-processing
	vext.8	d16, d28, d28, #1	@ A1
	vmull.p8	q8, d16, d6		@ F = A1*B
	vext.8	d2, d6, d6, #1	@ B1
	vmull.p8	q1, d28, d2		@ E = A*B1
	vext.8	d18, d28, d28, #2	@ A2
	vmull.p8	q9, d18, d6		@ H = A2*B
	vext.8	d22, d6, d6, #2	@ B2
	vmull.p8	q11, d28, d22		@ G = A*B2
	vext.8	d20, d28, d28, #3	@ A3
	veor	q8, q8, q1		@ L = E + F
	vmull.p8	q10, d20, d6		@ J = A3*B
	vext.8	d2, d6, d6, #3	@ B3
	veor	q9, q9, q11		@ M = G + H
	vmull.p8	q1, d28, d2		@ I = A*B3
	veor	d16, d16, d17	@ t0 = (L) (P0 + P1) << 8
	vand	d17, d17, d29
	vext.8	d22, d6, d6, #4	@ B4
	veor	d18, d18, d19	@ t1 = (M) (P2 + P3) << 16
	vand	d19, d19, d30
	vmull.p8	q11, d28, d22		@ K = A*B4
	veor	q10, q10, q1		@ N = I + J
	veor	d16, d16, d17
	veor	d18, d18, d19
	veor	d20, d20, d21	@ t2 = (N) (P4 + P5) << 24
	vand	d21, d21, d31
	vext.8	q8, q8, q8, #15
	veor	d22, d22, d23	@ t3 = (K) (P6 + P7) << 32
	vmov.i64	d23, #0
	vext.8	q9, q9, q9, #14
	veor	d20, d20, d21
	vmull.p8	q1, d28, d6		@ D = A*B
	vext.8	q11, q11, q11, #12
	vext.8	q10, q10, q10, #13
	veor	q8, q8, q9
	veor	q10, q10, q11
	veor	q1, q1, q8
	veor	q1, q1, q10
	vext.8	d16, d27, d27, #1	@ A1
	vmull.p8	q8, d16, d7		@ F = A1*B
	vext.8	d4, d7, d7, #1	@ B1
	vmull.p8	q2, d27, d4		@ E = A*B1
	vext.8	d18, d27, d27, #2	@ A2
	vmull.p8	q9, d18, d7		@ H = A2*B
	vext.8	d22, d7, d7, #2	@ B2
	vmull.p8	q11, d27, d22		@ G = A*B2
	vext.8	d20, d27, d27, #3	@ A3
	veor	q8, q8, q2		@ L = E + F
	vmull.p8	q10, d20, d7		@ J = A3*B
	vext.8	d4, d7, d7, #3	@ B3
	veor	q9, q9, q11		@ M = G + H
	vmull.p8	q2, d27, d4		@ I = A*B3
	veor	d16, d16, d17	@ t0 = (L) (P0 + P1) << 8
	vand	d17, d17, d29
	vext.8	d22, d7, d7, #4	@ B4
	veor	d18, d18, d19	@ t1 = (M) (P2 + P3) << 16
	vand	d19, d19, d30
	vmull.p8	q11, d27, d22		@ K = A*B4
	veor	q10, q10, q2		@ N = I + J
	veor	d16, d16, d17
	veor	d18, d18, d19
	veor	d20, d20, d21	@ t2 = (N) (P4 + P5) << 24
	vand	d21, d21, d31
	vext.8	q8, q8, q8, #15
	veor	d22, d22, d23	@ t3 = (K) (P6 + P7) << 32
	vmov.i64	d23, #0
	vext.8	q9, q9, q9, #14
	veor	d20, d20, d21
	vmull.p8	q2, d27, d7		@ D = A*B
	vext.8	q11, q11, q11, #12
	vext.8	q10, q10, q10, #13
	veor	q8, q8, q9
	veor	q10, q10, q11
	veor	q2, q2, q8
	veor	q2, q2, q10
	veor	q1,q1,q0		@ Karatsuba post-processing
	veor	q1,q1,q2
	veor	d1,d1,d2
	veor	d4,d4,d3	@ Xh|Xl - 256-bit result

	@ equivalent of reduction_avx from ghash-x86_64.pl
	vshl.i64	q9,q0,#57		@ 1st phase
	vshl.i64	q10,q0,#62
	veor	q10,q10,q9		@
	vshl.i64	q9,q0,#63
	veor	q10, q10, q9		@
	veor	d1,d1,d20	@
	veor	d4,d4,d21

	vshr.u64	q10,q0,#1		@ 2nd phase
	veor	q2,q2,q0
	veor	q0,q0,q10		@
	vshr.u64	q10,q10,#6
	vshr.u64	q0,q0,#1		@
	veor	q0,q0,q2		@
	veor	q0,q0,q10		@

	subs	r3,#16
	bne	.Loop_neon

#ifdef __ARMEL__
	vrev64.8	q0,q0
#endif
	sub	r0,#16
	vst1.64	d1,[r0]!		@ write out Xi
	vst1.64	d0,[r0]

	bx	lr					@ bx lr
.size	gcm_ghash_neon,.-gcm_ghash_neon
#endif
.byte	71,72,65,83,72,32,102,111,114,32,65,82,77,118,52,47,78,69,79,78,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	2
