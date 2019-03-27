/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from armv4-mont.pl. */
#include "arm_arch.h"

.text
#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif

#if __ARM_MAX_ARCH__>=7
.align	5
.LOPENSSL_armcap:
.word	OPENSSL_armcap_P-.Lbn_mul_mont
#endif

.globl	bn_mul_mont
.type	bn_mul_mont,%function

.align	5
bn_mul_mont:
.Lbn_mul_mont:
	ldr	ip,[sp,#4]		@ load num
	stmdb	sp!,{r0,r2}		@ sp points at argument block
#if __ARM_MAX_ARCH__>=7
	tst	ip,#7
	bne	.Lialu
	adr	r0,.Lbn_mul_mont
	ldr	r2,.LOPENSSL_armcap
	ldr	r0,[r0,r2]
#ifdef	__APPLE__
	ldr	r0,[r0]
#endif
	tst	r0,#ARMV7_NEON		@ NEON available?
	ldmia	sp, {r0,r2}
	beq	.Lialu
	add	sp,sp,#8
	b	bn_mul8x_mont_neon
.align	4
.Lialu:
#endif
	cmp	ip,#2
	mov	r0,ip			@ load num
#ifdef	__thumb2__
	ittt	lt
#endif
	movlt	r0,#0
	addlt	sp,sp,#2*4
	blt	.Labrt

	stmdb	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}		@ save 10 registers

	mov	r0,r0,lsl#2		@ rescale r0 for byte count
	sub	sp,sp,r0		@ alloca(4*num)
	sub	sp,sp,#4		@ +extra dword
	sub	r0,r0,#4		@ "num=num-1"
	add	r4,r2,r0		@ &bp[num-1]

	add	r0,sp,r0		@ r0 to point at &tp[num-1]
	ldr	r8,[r0,#14*4]		@ &n0
	ldr	r2,[r2]		@ bp[0]
	ldr	r5,[r1],#4		@ ap[0],ap++
	ldr	r6,[r3],#4		@ np[0],np++
	ldr	r8,[r8]		@ *n0
	str	r4,[r0,#15*4]		@ save &bp[num]

	umull	r10,r11,r5,r2	@ ap[0]*bp[0]
	str	r8,[r0,#14*4]		@ save n0 value
	mul	r8,r10,r8		@ "tp[0]"*n0
	mov	r12,#0
	umlal	r10,r12,r6,r8	@ np[0]*n0+"t[0]"
	mov	r4,sp

.L1st:
	ldr	r5,[r1],#4		@ ap[j],ap++
	mov	r10,r11
	ldr	r6,[r3],#4		@ np[j],np++
	mov	r11,#0
	umlal	r10,r11,r5,r2	@ ap[j]*bp[0]
	mov	r14,#0
	umlal	r12,r14,r6,r8	@ np[j]*n0
	adds	r12,r12,r10
	str	r12,[r4],#4		@ tp[j-1]=,tp++
	adc	r12,r14,#0
	cmp	r4,r0
	bne	.L1st

	adds	r12,r12,r11
	ldr	r4,[r0,#13*4]		@ restore bp
	mov	r14,#0
	ldr	r8,[r0,#14*4]		@ restore n0
	adc	r14,r14,#0
	str	r12,[r0]		@ tp[num-1]=
	mov	r7,sp
	str	r14,[r0,#4]		@ tp[num]=

.Louter:
	sub	r7,r0,r7		@ "original" r0-1 value
	sub	r1,r1,r7		@ "rewind" ap to &ap[1]
	ldr	r2,[r4,#4]!		@ *(++bp)
	sub	r3,r3,r7		@ "rewind" np to &np[1]
	ldr	r5,[r1,#-4]		@ ap[0]
	ldr	r10,[sp]		@ tp[0]
	ldr	r6,[r3,#-4]		@ np[0]
	ldr	r7,[sp,#4]		@ tp[1]

	mov	r11,#0
	umlal	r10,r11,r5,r2	@ ap[0]*bp[i]+tp[0]
	str	r4,[r0,#13*4]		@ save bp
	mul	r8,r10,r8
	mov	r12,#0
	umlal	r10,r12,r6,r8	@ np[0]*n0+"tp[0]"
	mov	r4,sp

.Linner:
	ldr	r5,[r1],#4		@ ap[j],ap++
	adds	r10,r11,r7		@ +=tp[j]
	ldr	r6,[r3],#4		@ np[j],np++
	mov	r11,#0
	umlal	r10,r11,r5,r2	@ ap[j]*bp[i]
	mov	r14,#0
	umlal	r12,r14,r6,r8	@ np[j]*n0
	adc	r11,r11,#0
	ldr	r7,[r4,#8]		@ tp[j+1]
	adds	r12,r12,r10
	str	r12,[r4],#4		@ tp[j-1]=,tp++
	adc	r12,r14,#0
	cmp	r4,r0
	bne	.Linner

	adds	r12,r12,r11
	mov	r14,#0
	ldr	r4,[r0,#13*4]		@ restore bp
	adc	r14,r14,#0
	ldr	r8,[r0,#14*4]		@ restore n0
	adds	r12,r12,r7
	ldr	r7,[r0,#15*4]		@ restore &bp[num]
	adc	r14,r14,#0
	str	r12,[r0]		@ tp[num-1]=
	str	r14,[r0,#4]		@ tp[num]=

	cmp	r4,r7
#ifdef	__thumb2__
	itt	ne
#endif
	movne	r7,sp
	bne	.Louter

	ldr	r2,[r0,#12*4]		@ pull rp
	mov	r5,sp
	add	r0,r0,#4		@ r0 to point at &tp[num]
	sub	r5,r0,r5		@ "original" num value
	mov	r4,sp			@ "rewind" r4
	mov	r1,r4			@ "borrow" r1
	sub	r3,r3,r5		@ "rewind" r3 to &np[0]

	subs	r7,r7,r7		@ "clear" carry flag
.Lsub:	ldr	r7,[r4],#4
	ldr	r6,[r3],#4
	sbcs	r7,r7,r6		@ tp[j]-np[j]
	str	r7,[r2],#4		@ rp[j]=
	teq	r4,r0		@ preserve carry
	bne	.Lsub
	sbcs	r14,r14,#0		@ upmost carry
	mov	r4,sp			@ "rewind" r4
	sub	r2,r2,r5		@ "rewind" r2

.Lcopy:	ldr	r7,[r4]		@ conditional copy
	ldr	r5,[r2]
	str	sp,[r4],#4		@ zap tp
#ifdef	__thumb2__
	it	cc
#endif
	movcc	r5,r7
	str	r5,[r2],#4
	teq	r4,r0		@ preserve carry
	bne	.Lcopy

	mov	sp,r0
	add	sp,sp,#4		@ skip over tp[num+1]
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}		@ restore registers
	add	sp,sp,#2*4		@ skip over {r0,r2}
	mov	r0,#1
.Labrt:
#if __ARM_ARCH__>=5
	bx	lr				@ bx lr
#else
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
.size	bn_mul_mont,.-bn_mul_mont
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.type	bn_mul8x_mont_neon,%function
.align	5
bn_mul8x_mont_neon:
	mov	ip,sp
	stmdb	sp!,{r4,r5,r6,r7,r8,r9,r10,r11}
	vstmdb	sp!,{d8,d9,d10,d11,d12,d13,d14,d15}		@ ABI specification says so
	ldmia	ip,{r4,r5}		@ load rest of parameter block
	mov	ip,sp

	cmp	r5,#8
	bhi	.LNEON_8n

	@ special case for r5==8, everything is in register bank...

	vld1.32	{d28[0]}, [r2,:32]!
	veor	d8,d8,d8
	sub	r7,sp,r5,lsl#4
	vld1.32	{d0,d1,d2,d3},  [r1]!		@ can't specify :32 :-(
	and	r7,r7,#-64
	vld1.32	{d30[0]}, [r4,:32]
	mov	sp,r7			@ alloca
	vzip.16	d28,d8

	vmull.u32	q6,d28,d0[0]
	vmull.u32	q7,d28,d0[1]
	vmull.u32	q8,d28,d1[0]
	vshl.i64	d29,d13,#16
	vmull.u32	q9,d28,d1[1]

	vadd.u64	d29,d29,d12
	veor	d8,d8,d8
	vmul.u32	d29,d29,d30

	vmull.u32	q10,d28,d2[0]
	vld1.32	{d4,d5,d6,d7}, [r3]!
	vmull.u32	q11,d28,d2[1]
	vmull.u32	q12,d28,d3[0]
	vzip.16	d29,d8
	vmull.u32	q13,d28,d3[1]

	vmlal.u32	q6,d29,d4[0]
	sub	r9,r5,#1
	vmlal.u32	q7,d29,d4[1]
	vmlal.u32	q8,d29,d5[0]
	vmlal.u32	q9,d29,d5[1]

	vmlal.u32	q10,d29,d6[0]
	vmov	q5,q6
	vmlal.u32	q11,d29,d6[1]
	vmov	q6,q7
	vmlal.u32	q12,d29,d7[0]
	vmov	q7,q8
	vmlal.u32	q13,d29,d7[1]
	vmov	q8,q9
	vmov	q9,q10
	vshr.u64	d10,d10,#16
	vmov	q10,q11
	vmov	q11,q12
	vadd.u64	d10,d10,d11
	vmov	q12,q13
	veor	q13,q13
	vshr.u64	d10,d10,#16

	b	.LNEON_outer8

.align	4
.LNEON_outer8:
	vld1.32	{d28[0]}, [r2,:32]!
	veor	d8,d8,d8
	vzip.16	d28,d8
	vadd.u64	d12,d12,d10

	vmlal.u32	q6,d28,d0[0]
	vmlal.u32	q7,d28,d0[1]
	vmlal.u32	q8,d28,d1[0]
	vshl.i64	d29,d13,#16
	vmlal.u32	q9,d28,d1[1]

	vadd.u64	d29,d29,d12
	veor	d8,d8,d8
	subs	r9,r9,#1
	vmul.u32	d29,d29,d30

	vmlal.u32	q10,d28,d2[0]
	vmlal.u32	q11,d28,d2[1]
	vmlal.u32	q12,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q13,d28,d3[1]

	vmlal.u32	q6,d29,d4[0]
	vmlal.u32	q7,d29,d4[1]
	vmlal.u32	q8,d29,d5[0]
	vmlal.u32	q9,d29,d5[1]

	vmlal.u32	q10,d29,d6[0]
	vmov	q5,q6
	vmlal.u32	q11,d29,d6[1]
	vmov	q6,q7
	vmlal.u32	q12,d29,d7[0]
	vmov	q7,q8
	vmlal.u32	q13,d29,d7[1]
	vmov	q8,q9
	vmov	q9,q10
	vshr.u64	d10,d10,#16
	vmov	q10,q11
	vmov	q11,q12
	vadd.u64	d10,d10,d11
	vmov	q12,q13
	veor	q13,q13
	vshr.u64	d10,d10,#16

	bne	.LNEON_outer8

	vadd.u64	d12,d12,d10
	mov	r7,sp
	vshr.u64	d10,d12,#16
	mov	r8,r5
	vadd.u64	d13,d13,d10
	add	r6,sp,#96
	vshr.u64	d10,d13,#16
	vzip.16	d12,d13

	b	.LNEON_tail_entry

.align	4
.LNEON_8n:
	veor	q6,q6,q6
	sub	r7,sp,#128
	veor	q7,q7,q7
	sub	r7,r7,r5,lsl#4
	veor	q8,q8,q8
	and	r7,r7,#-64
	veor	q9,q9,q9
	mov	sp,r7			@ alloca
	veor	q10,q10,q10
	add	r7,r7,#256
	veor	q11,q11,q11
	sub	r8,r5,#8
	veor	q12,q12,q12
	veor	q13,q13,q13

.LNEON_8n_init:
	vst1.64	{q6,q7},[r7,:256]!
	subs	r8,r8,#8
	vst1.64	{q8,q9},[r7,:256]!
	vst1.64	{q10,q11},[r7,:256]!
	vst1.64	{q12,q13},[r7,:256]!
	bne	.LNEON_8n_init

	add	r6,sp,#256
	vld1.32	{d0,d1,d2,d3},[r1]!
	add	r10,sp,#8
	vld1.32	{d30[0]},[r4,:32]
	mov	r9,r5
	b	.LNEON_8n_outer

.align	4
.LNEON_8n_outer:
	vld1.32	{d28[0]},[r2,:32]!	@ *b++
	veor	d8,d8,d8
	vzip.16	d28,d8
	add	r7,sp,#128
	vld1.32	{d4,d5,d6,d7},[r3]!

	vmlal.u32	q6,d28,d0[0]
	vmlal.u32	q7,d28,d0[1]
	veor	d8,d8,d8
	vmlal.u32	q8,d28,d1[0]
	vshl.i64	d29,d13,#16
	vmlal.u32	q9,d28,d1[1]
	vadd.u64	d29,d29,d12
	vmlal.u32	q10,d28,d2[0]
	vmul.u32	d29,d29,d30
	vmlal.u32	q11,d28,d2[1]
	vst1.32	{d28},[sp,:64]		@ put aside smashed b[8*i+0]
	vmlal.u32	q12,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q13,d28,d3[1]
	vld1.32	{d28[0]},[r2,:32]!	@ *b++
	vmlal.u32	q6,d29,d4[0]
	veor	d10,d10,d10
	vmlal.u32	q7,d29,d4[1]
	vzip.16	d28,d10
	vmlal.u32	q8,d29,d5[0]
	vshr.u64	d12,d12,#16
	vmlal.u32	q9,d29,d5[1]
	vmlal.u32	q10,d29,d6[0]
	vadd.u64	d12,d12,d13
	vmlal.u32	q11,d29,d6[1]
	vshr.u64	d12,d12,#16
	vmlal.u32	q12,d29,d7[0]
	vmlal.u32	q13,d29,d7[1]
	vadd.u64	d14,d14,d12
	vst1.32	{d29},[r10,:64]!	@ put aside smashed m[8*i+0]
	vmlal.u32	q7,d28,d0[0]
	vld1.64	{q6},[r6,:128]!
	vmlal.u32	q8,d28,d0[1]
	veor	d8,d8,d8
	vmlal.u32	q9,d28,d1[0]
	vshl.i64	d29,d15,#16
	vmlal.u32	q10,d28,d1[1]
	vadd.u64	d29,d29,d14
	vmlal.u32	q11,d28,d2[0]
	vmul.u32	d29,d29,d30
	vmlal.u32	q12,d28,d2[1]
	vst1.32	{d28},[r10,:64]!	@ put aside smashed b[8*i+1]
	vmlal.u32	q13,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q6,d28,d3[1]
	vld1.32	{d28[0]},[r2,:32]!	@ *b++
	vmlal.u32	q7,d29,d4[0]
	veor	d10,d10,d10
	vmlal.u32	q8,d29,d4[1]
	vzip.16	d28,d10
	vmlal.u32	q9,d29,d5[0]
	vshr.u64	d14,d14,#16
	vmlal.u32	q10,d29,d5[1]
	vmlal.u32	q11,d29,d6[0]
	vadd.u64	d14,d14,d15
	vmlal.u32	q12,d29,d6[1]
	vshr.u64	d14,d14,#16
	vmlal.u32	q13,d29,d7[0]
	vmlal.u32	q6,d29,d7[1]
	vadd.u64	d16,d16,d14
	vst1.32	{d29},[r10,:64]!	@ put aside smashed m[8*i+1]
	vmlal.u32	q8,d28,d0[0]
	vld1.64	{q7},[r6,:128]!
	vmlal.u32	q9,d28,d0[1]
	veor	d8,d8,d8
	vmlal.u32	q10,d28,d1[0]
	vshl.i64	d29,d17,#16
	vmlal.u32	q11,d28,d1[1]
	vadd.u64	d29,d29,d16
	vmlal.u32	q12,d28,d2[0]
	vmul.u32	d29,d29,d30
	vmlal.u32	q13,d28,d2[1]
	vst1.32	{d28},[r10,:64]!	@ put aside smashed b[8*i+2]
	vmlal.u32	q6,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q7,d28,d3[1]
	vld1.32	{d28[0]},[r2,:32]!	@ *b++
	vmlal.u32	q8,d29,d4[0]
	veor	d10,d10,d10
	vmlal.u32	q9,d29,d4[1]
	vzip.16	d28,d10
	vmlal.u32	q10,d29,d5[0]
	vshr.u64	d16,d16,#16
	vmlal.u32	q11,d29,d5[1]
	vmlal.u32	q12,d29,d6[0]
	vadd.u64	d16,d16,d17
	vmlal.u32	q13,d29,d6[1]
	vshr.u64	d16,d16,#16
	vmlal.u32	q6,d29,d7[0]
	vmlal.u32	q7,d29,d7[1]
	vadd.u64	d18,d18,d16
	vst1.32	{d29},[r10,:64]!	@ put aside smashed m[8*i+2]
	vmlal.u32	q9,d28,d0[0]
	vld1.64	{q8},[r6,:128]!
	vmlal.u32	q10,d28,d0[1]
	veor	d8,d8,d8
	vmlal.u32	q11,d28,d1[0]
	vshl.i64	d29,d19,#16
	vmlal.u32	q12,d28,d1[1]
	vadd.u64	d29,d29,d18
	vmlal.u32	q13,d28,d2[0]
	vmul.u32	d29,d29,d30
	vmlal.u32	q6,d28,d2[1]
	vst1.32	{d28},[r10,:64]!	@ put aside smashed b[8*i+3]
	vmlal.u32	q7,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q8,d28,d3[1]
	vld1.32	{d28[0]},[r2,:32]!	@ *b++
	vmlal.u32	q9,d29,d4[0]
	veor	d10,d10,d10
	vmlal.u32	q10,d29,d4[1]
	vzip.16	d28,d10
	vmlal.u32	q11,d29,d5[0]
	vshr.u64	d18,d18,#16
	vmlal.u32	q12,d29,d5[1]
	vmlal.u32	q13,d29,d6[0]
	vadd.u64	d18,d18,d19
	vmlal.u32	q6,d29,d6[1]
	vshr.u64	d18,d18,#16
	vmlal.u32	q7,d29,d7[0]
	vmlal.u32	q8,d29,d7[1]
	vadd.u64	d20,d20,d18
	vst1.32	{d29},[r10,:64]!	@ put aside smashed m[8*i+3]
	vmlal.u32	q10,d28,d0[0]
	vld1.64	{q9},[r6,:128]!
	vmlal.u32	q11,d28,d0[1]
	veor	d8,d8,d8
	vmlal.u32	q12,d28,d1[0]
	vshl.i64	d29,d21,#16
	vmlal.u32	q13,d28,d1[1]
	vadd.u64	d29,d29,d20
	vmlal.u32	q6,d28,d2[0]
	vmul.u32	d29,d29,d30
	vmlal.u32	q7,d28,d2[1]
	vst1.32	{d28},[r10,:64]!	@ put aside smashed b[8*i+4]
	vmlal.u32	q8,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q9,d28,d3[1]
	vld1.32	{d28[0]},[r2,:32]!	@ *b++
	vmlal.u32	q10,d29,d4[0]
	veor	d10,d10,d10
	vmlal.u32	q11,d29,d4[1]
	vzip.16	d28,d10
	vmlal.u32	q12,d29,d5[0]
	vshr.u64	d20,d20,#16
	vmlal.u32	q13,d29,d5[1]
	vmlal.u32	q6,d29,d6[0]
	vadd.u64	d20,d20,d21
	vmlal.u32	q7,d29,d6[1]
	vshr.u64	d20,d20,#16
	vmlal.u32	q8,d29,d7[0]
	vmlal.u32	q9,d29,d7[1]
	vadd.u64	d22,d22,d20
	vst1.32	{d29},[r10,:64]!	@ put aside smashed m[8*i+4]
	vmlal.u32	q11,d28,d0[0]
	vld1.64	{q10},[r6,:128]!
	vmlal.u32	q12,d28,d0[1]
	veor	d8,d8,d8
	vmlal.u32	q13,d28,d1[0]
	vshl.i64	d29,d23,#16
	vmlal.u32	q6,d28,d1[1]
	vadd.u64	d29,d29,d22
	vmlal.u32	q7,d28,d2[0]
	vmul.u32	d29,d29,d30
	vmlal.u32	q8,d28,d2[1]
	vst1.32	{d28},[r10,:64]!	@ put aside smashed b[8*i+5]
	vmlal.u32	q9,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q10,d28,d3[1]
	vld1.32	{d28[0]},[r2,:32]!	@ *b++
	vmlal.u32	q11,d29,d4[0]
	veor	d10,d10,d10
	vmlal.u32	q12,d29,d4[1]
	vzip.16	d28,d10
	vmlal.u32	q13,d29,d5[0]
	vshr.u64	d22,d22,#16
	vmlal.u32	q6,d29,d5[1]
	vmlal.u32	q7,d29,d6[0]
	vadd.u64	d22,d22,d23
	vmlal.u32	q8,d29,d6[1]
	vshr.u64	d22,d22,#16
	vmlal.u32	q9,d29,d7[0]
	vmlal.u32	q10,d29,d7[1]
	vadd.u64	d24,d24,d22
	vst1.32	{d29},[r10,:64]!	@ put aside smashed m[8*i+5]
	vmlal.u32	q12,d28,d0[0]
	vld1.64	{q11},[r6,:128]!
	vmlal.u32	q13,d28,d0[1]
	veor	d8,d8,d8
	vmlal.u32	q6,d28,d1[0]
	vshl.i64	d29,d25,#16
	vmlal.u32	q7,d28,d1[1]
	vadd.u64	d29,d29,d24
	vmlal.u32	q8,d28,d2[0]
	vmul.u32	d29,d29,d30
	vmlal.u32	q9,d28,d2[1]
	vst1.32	{d28},[r10,:64]!	@ put aside smashed b[8*i+6]
	vmlal.u32	q10,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q11,d28,d3[1]
	vld1.32	{d28[0]},[r2,:32]!	@ *b++
	vmlal.u32	q12,d29,d4[0]
	veor	d10,d10,d10
	vmlal.u32	q13,d29,d4[1]
	vzip.16	d28,d10
	vmlal.u32	q6,d29,d5[0]
	vshr.u64	d24,d24,#16
	vmlal.u32	q7,d29,d5[1]
	vmlal.u32	q8,d29,d6[0]
	vadd.u64	d24,d24,d25
	vmlal.u32	q9,d29,d6[1]
	vshr.u64	d24,d24,#16
	vmlal.u32	q10,d29,d7[0]
	vmlal.u32	q11,d29,d7[1]
	vadd.u64	d26,d26,d24
	vst1.32	{d29},[r10,:64]!	@ put aside smashed m[8*i+6]
	vmlal.u32	q13,d28,d0[0]
	vld1.64	{q12},[r6,:128]!
	vmlal.u32	q6,d28,d0[1]
	veor	d8,d8,d8
	vmlal.u32	q7,d28,d1[0]
	vshl.i64	d29,d27,#16
	vmlal.u32	q8,d28,d1[1]
	vadd.u64	d29,d29,d26
	vmlal.u32	q9,d28,d2[0]
	vmul.u32	d29,d29,d30
	vmlal.u32	q10,d28,d2[1]
	vst1.32	{d28},[r10,:64]!	@ put aside smashed b[8*i+7]
	vmlal.u32	q11,d28,d3[0]
	vzip.16	d29,d8
	vmlal.u32	q12,d28,d3[1]
	vld1.32	{d28},[sp,:64]		@ pull smashed b[8*i+0]
	vmlal.u32	q13,d29,d4[0]
	vld1.32	{d0,d1,d2,d3},[r1]!
	vmlal.u32	q6,d29,d4[1]
	vmlal.u32	q7,d29,d5[0]
	vshr.u64	d26,d26,#16
	vmlal.u32	q8,d29,d5[1]
	vmlal.u32	q9,d29,d6[0]
	vadd.u64	d26,d26,d27
	vmlal.u32	q10,d29,d6[1]
	vshr.u64	d26,d26,#16
	vmlal.u32	q11,d29,d7[0]
	vmlal.u32	q12,d29,d7[1]
	vadd.u64	d12,d12,d26
	vst1.32	{d29},[r10,:64]	@ put aside smashed m[8*i+7]
	add	r10,sp,#8		@ rewind
	sub	r8,r5,#8
	b	.LNEON_8n_inner

.align	4
.LNEON_8n_inner:
	subs	r8,r8,#8
	vmlal.u32	q6,d28,d0[0]
	vld1.64	{q13},[r6,:128]
	vmlal.u32	q7,d28,d0[1]
	vld1.32	{d29},[r10,:64]!	@ pull smashed m[8*i+0]
	vmlal.u32	q8,d28,d1[0]
	vld1.32	{d4,d5,d6,d7},[r3]!
	vmlal.u32	q9,d28,d1[1]
	it	ne
	addne	r6,r6,#16	@ don't advance in last iteration
	vmlal.u32	q10,d28,d2[0]
	vmlal.u32	q11,d28,d2[1]
	vmlal.u32	q12,d28,d3[0]
	vmlal.u32	q13,d28,d3[1]
	vld1.32	{d28},[r10,:64]!	@ pull smashed b[8*i+1]
	vmlal.u32	q6,d29,d4[0]
	vmlal.u32	q7,d29,d4[1]
	vmlal.u32	q8,d29,d5[0]
	vmlal.u32	q9,d29,d5[1]
	vmlal.u32	q10,d29,d6[0]
	vmlal.u32	q11,d29,d6[1]
	vmlal.u32	q12,d29,d7[0]
	vmlal.u32	q13,d29,d7[1]
	vst1.64	{q6},[r7,:128]!
	vmlal.u32	q7,d28,d0[0]
	vld1.64	{q6},[r6,:128]
	vmlal.u32	q8,d28,d0[1]
	vld1.32	{d29},[r10,:64]!	@ pull smashed m[8*i+1]
	vmlal.u32	q9,d28,d1[0]
	it	ne
	addne	r6,r6,#16	@ don't advance in last iteration
	vmlal.u32	q10,d28,d1[1]
	vmlal.u32	q11,d28,d2[0]
	vmlal.u32	q12,d28,d2[1]
	vmlal.u32	q13,d28,d3[0]
	vmlal.u32	q6,d28,d3[1]
	vld1.32	{d28},[r10,:64]!	@ pull smashed b[8*i+2]
	vmlal.u32	q7,d29,d4[0]
	vmlal.u32	q8,d29,d4[1]
	vmlal.u32	q9,d29,d5[0]
	vmlal.u32	q10,d29,d5[1]
	vmlal.u32	q11,d29,d6[0]
	vmlal.u32	q12,d29,d6[1]
	vmlal.u32	q13,d29,d7[0]
	vmlal.u32	q6,d29,d7[1]
	vst1.64	{q7},[r7,:128]!
	vmlal.u32	q8,d28,d0[0]
	vld1.64	{q7},[r6,:128]
	vmlal.u32	q9,d28,d0[1]
	vld1.32	{d29},[r10,:64]!	@ pull smashed m[8*i+2]
	vmlal.u32	q10,d28,d1[0]
	it	ne
	addne	r6,r6,#16	@ don't advance in last iteration
	vmlal.u32	q11,d28,d1[1]
	vmlal.u32	q12,d28,d2[0]
	vmlal.u32	q13,d28,d2[1]
	vmlal.u32	q6,d28,d3[0]
	vmlal.u32	q7,d28,d3[1]
	vld1.32	{d28},[r10,:64]!	@ pull smashed b[8*i+3]
	vmlal.u32	q8,d29,d4[0]
	vmlal.u32	q9,d29,d4[1]
	vmlal.u32	q10,d29,d5[0]
	vmlal.u32	q11,d29,d5[1]
	vmlal.u32	q12,d29,d6[0]
	vmlal.u32	q13,d29,d6[1]
	vmlal.u32	q6,d29,d7[0]
	vmlal.u32	q7,d29,d7[1]
	vst1.64	{q8},[r7,:128]!
	vmlal.u32	q9,d28,d0[0]
	vld1.64	{q8},[r6,:128]
	vmlal.u32	q10,d28,d0[1]
	vld1.32	{d29},[r10,:64]!	@ pull smashed m[8*i+3]
	vmlal.u32	q11,d28,d1[0]
	it	ne
	addne	r6,r6,#16	@ don't advance in last iteration
	vmlal.u32	q12,d28,d1[1]
	vmlal.u32	q13,d28,d2[0]
	vmlal.u32	q6,d28,d2[1]
	vmlal.u32	q7,d28,d3[0]
	vmlal.u32	q8,d28,d3[1]
	vld1.32	{d28},[r10,:64]!	@ pull smashed b[8*i+4]
	vmlal.u32	q9,d29,d4[0]
	vmlal.u32	q10,d29,d4[1]
	vmlal.u32	q11,d29,d5[0]
	vmlal.u32	q12,d29,d5[1]
	vmlal.u32	q13,d29,d6[0]
	vmlal.u32	q6,d29,d6[1]
	vmlal.u32	q7,d29,d7[0]
	vmlal.u32	q8,d29,d7[1]
	vst1.64	{q9},[r7,:128]!
	vmlal.u32	q10,d28,d0[0]
	vld1.64	{q9},[r6,:128]
	vmlal.u32	q11,d28,d0[1]
	vld1.32	{d29},[r10,:64]!	@ pull smashed m[8*i+4]
	vmlal.u32	q12,d28,d1[0]
	it	ne
	addne	r6,r6,#16	@ don't advance in last iteration
	vmlal.u32	q13,d28,d1[1]
	vmlal.u32	q6,d28,d2[0]
	vmlal.u32	q7,d28,d2[1]
	vmlal.u32	q8,d28,d3[0]
	vmlal.u32	q9,d28,d3[1]
	vld1.32	{d28},[r10,:64]!	@ pull smashed b[8*i+5]
	vmlal.u32	q10,d29,d4[0]
	vmlal.u32	q11,d29,d4[1]
	vmlal.u32	q12,d29,d5[0]
	vmlal.u32	q13,d29,d5[1]
	vmlal.u32	q6,d29,d6[0]
	vmlal.u32	q7,d29,d6[1]
	vmlal.u32	q8,d29,d7[0]
	vmlal.u32	q9,d29,d7[1]
	vst1.64	{q10},[r7,:128]!
	vmlal.u32	q11,d28,d0[0]
	vld1.64	{q10},[r6,:128]
	vmlal.u32	q12,d28,d0[1]
	vld1.32	{d29},[r10,:64]!	@ pull smashed m[8*i+5]
	vmlal.u32	q13,d28,d1[0]
	it	ne
	addne	r6,r6,#16	@ don't advance in last iteration
	vmlal.u32	q6,d28,d1[1]
	vmlal.u32	q7,d28,d2[0]
	vmlal.u32	q8,d28,d2[1]
	vmlal.u32	q9,d28,d3[0]
	vmlal.u32	q10,d28,d3[1]
	vld1.32	{d28},[r10,:64]!	@ pull smashed b[8*i+6]
	vmlal.u32	q11,d29,d4[0]
	vmlal.u32	q12,d29,d4[1]
	vmlal.u32	q13,d29,d5[0]
	vmlal.u32	q6,d29,d5[1]
	vmlal.u32	q7,d29,d6[0]
	vmlal.u32	q8,d29,d6[1]
	vmlal.u32	q9,d29,d7[0]
	vmlal.u32	q10,d29,d7[1]
	vst1.64	{q11},[r7,:128]!
	vmlal.u32	q12,d28,d0[0]
	vld1.64	{q11},[r6,:128]
	vmlal.u32	q13,d28,d0[1]
	vld1.32	{d29},[r10,:64]!	@ pull smashed m[8*i+6]
	vmlal.u32	q6,d28,d1[0]
	it	ne
	addne	r6,r6,#16	@ don't advance in last iteration
	vmlal.u32	q7,d28,d1[1]
	vmlal.u32	q8,d28,d2[0]
	vmlal.u32	q9,d28,d2[1]
	vmlal.u32	q10,d28,d3[0]
	vmlal.u32	q11,d28,d3[1]
	vld1.32	{d28},[r10,:64]!	@ pull smashed b[8*i+7]
	vmlal.u32	q12,d29,d4[0]
	vmlal.u32	q13,d29,d4[1]
	vmlal.u32	q6,d29,d5[0]
	vmlal.u32	q7,d29,d5[1]
	vmlal.u32	q8,d29,d6[0]
	vmlal.u32	q9,d29,d6[1]
	vmlal.u32	q10,d29,d7[0]
	vmlal.u32	q11,d29,d7[1]
	vst1.64	{q12},[r7,:128]!
	vmlal.u32	q13,d28,d0[0]
	vld1.64	{q12},[r6,:128]
	vmlal.u32	q6,d28,d0[1]
	vld1.32	{d29},[r10,:64]!	@ pull smashed m[8*i+7]
	vmlal.u32	q7,d28,d1[0]
	it	ne
	addne	r6,r6,#16	@ don't advance in last iteration
	vmlal.u32	q8,d28,d1[1]
	vmlal.u32	q9,d28,d2[0]
	vmlal.u32	q10,d28,d2[1]
	vmlal.u32	q11,d28,d3[0]
	vmlal.u32	q12,d28,d3[1]
	it	eq
	subeq	r1,r1,r5,lsl#2	@ rewind
	vmlal.u32	q13,d29,d4[0]
	vld1.32	{d28},[sp,:64]		@ pull smashed b[8*i+0]
	vmlal.u32	q6,d29,d4[1]
	vld1.32	{d0,d1,d2,d3},[r1]!
	vmlal.u32	q7,d29,d5[0]
	add	r10,sp,#8		@ rewind
	vmlal.u32	q8,d29,d5[1]
	vmlal.u32	q9,d29,d6[0]
	vmlal.u32	q10,d29,d6[1]
	vmlal.u32	q11,d29,d7[0]
	vst1.64	{q13},[r7,:128]!
	vmlal.u32	q12,d29,d7[1]

	bne	.LNEON_8n_inner
	add	r6,sp,#128
	vst1.64	{q6,q7},[r7,:256]!
	veor	q2,q2,q2		@ d4-d5
	vst1.64	{q8,q9},[r7,:256]!
	veor	q3,q3,q3		@ d6-d7
	vst1.64	{q10,q11},[r7,:256]!
	vst1.64	{q12},[r7,:128]

	subs	r9,r9,#8
	vld1.64	{q6,q7},[r6,:256]!
	vld1.64	{q8,q9},[r6,:256]!
	vld1.64	{q10,q11},[r6,:256]!
	vld1.64	{q12,q13},[r6,:256]!

	itt	ne
	subne	r3,r3,r5,lsl#2	@ rewind
	bne	.LNEON_8n_outer

	add	r7,sp,#128
	vst1.64	{q2,q3}, [sp,:256]!	@ start wiping stack frame
	vshr.u64	d10,d12,#16
	vst1.64	{q2,q3},[sp,:256]!
	vadd.u64	d13,d13,d10
	vst1.64	{q2,q3}, [sp,:256]!
	vshr.u64	d10,d13,#16
	vst1.64	{q2,q3}, [sp,:256]!
	vzip.16	d12,d13

	mov	r8,r5
	b	.LNEON_tail_entry

.align	4
.LNEON_tail:
	vadd.u64	d12,d12,d10
	vshr.u64	d10,d12,#16
	vld1.64	{q8,q9}, [r6, :256]!
	vadd.u64	d13,d13,d10
	vld1.64	{q10,q11}, [r6, :256]!
	vshr.u64	d10,d13,#16
	vld1.64	{q12,q13}, [r6, :256]!
	vzip.16	d12,d13

.LNEON_tail_entry:
	vadd.u64	d14,d14,d10
	vst1.32	{d12[0]}, [r7, :32]!
	vshr.u64	d10,d14,#16
	vadd.u64	d15,d15,d10
	vshr.u64	d10,d15,#16
	vzip.16	d14,d15
	vadd.u64	d16,d16,d10
	vst1.32	{d14[0]}, [r7, :32]!
	vshr.u64	d10,d16,#16
	vadd.u64	d17,d17,d10
	vshr.u64	d10,d17,#16
	vzip.16	d16,d17
	vadd.u64	d18,d18,d10
	vst1.32	{d16[0]}, [r7, :32]!
	vshr.u64	d10,d18,#16
	vadd.u64	d19,d19,d10
	vshr.u64	d10,d19,#16
	vzip.16	d18,d19
	vadd.u64	d20,d20,d10
	vst1.32	{d18[0]}, [r7, :32]!
	vshr.u64	d10,d20,#16
	vadd.u64	d21,d21,d10
	vshr.u64	d10,d21,#16
	vzip.16	d20,d21
	vadd.u64	d22,d22,d10
	vst1.32	{d20[0]}, [r7, :32]!
	vshr.u64	d10,d22,#16
	vadd.u64	d23,d23,d10
	vshr.u64	d10,d23,#16
	vzip.16	d22,d23
	vadd.u64	d24,d24,d10
	vst1.32	{d22[0]}, [r7, :32]!
	vshr.u64	d10,d24,#16
	vadd.u64	d25,d25,d10
	vshr.u64	d10,d25,#16
	vzip.16	d24,d25
	vadd.u64	d26,d26,d10
	vst1.32	{d24[0]}, [r7, :32]!
	vshr.u64	d10,d26,#16
	vadd.u64	d27,d27,d10
	vshr.u64	d10,d27,#16
	vzip.16	d26,d27
	vld1.64	{q6,q7}, [r6, :256]!
	subs	r8,r8,#8
	vst1.32	{d26[0]},   [r7, :32]!
	bne	.LNEON_tail

	vst1.32	{d10[0]}, [r7, :32]		@ top-most bit
	sub	r3,r3,r5,lsl#2			@ rewind r3
	subs	r1,sp,#0				@ clear carry flag
	add	r2,sp,r5,lsl#2

.LNEON_sub:
	ldmia	r1!, {r4,r5,r6,r7}
	ldmia	r3!, {r8,r9,r10,r11}
	sbcs	r8, r4,r8
	sbcs	r9, r5,r9
	sbcs	r10,r6,r10
	sbcs	r11,r7,r11
	teq	r1,r2				@ preserves carry
	stmia	r0!, {r8,r9,r10,r11}
	bne	.LNEON_sub

	ldr	r10, [r1]				@ load top-most bit
	mov	r11,sp
	veor	q0,q0,q0
	sub	r11,r2,r11				@ this is num*4
	veor	q1,q1,q1
	mov	r1,sp
	sub	r0,r0,r11				@ rewind r0
	mov	r3,r2				@ second 3/4th of frame
	sbcs	r10,r10,#0				@ result is carry flag

.LNEON_copy_n_zap:
	ldmia	r1!, {r4,r5,r6,r7}
	ldmia	r0,  {r8,r9,r10,r11}
	it	cc
	movcc	r8, r4
	vst1.64	{q0,q1}, [r3,:256]!			@ wipe
	itt	cc
	movcc	r9, r5
	movcc	r10,r6
	vst1.64	{q0,q1}, [r3,:256]!			@ wipe
	it	cc
	movcc	r11,r7
	ldmia	r1, {r4,r5,r6,r7}
	stmia	r0!, {r8,r9,r10,r11}
	sub	r1,r1,#16
	ldmia	r0, {r8,r9,r10,r11}
	it	cc
	movcc	r8, r4
	vst1.64	{q0,q1}, [r1,:256]!			@ wipe
	itt	cc
	movcc	r9, r5
	movcc	r10,r6
	vst1.64	{q0,q1}, [r3,:256]!			@ wipe
	it	cc
	movcc	r11,r7
	teq	r1,r2				@ preserves carry
	stmia	r0!, {r8,r9,r10,r11}
	bne	.LNEON_copy_n_zap

	mov	sp,ip
	vldmia	sp!,{d8,d9,d10,d11,d12,d13,d14,d15}
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11}
	bx	lr						@ bx lr
.size	bn_mul8x_mont_neon,.-bn_mul8x_mont_neon
#endif
.byte	77,111,110,116,103,111,109,101,114,121,32,109,117,108,116,105,112,108,105,99,97,116,105,111,110,32,102,111,114,32,65,82,77,118,52,47,78,69,79,78,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	2
#if __ARM_MAX_ARCH__>=7
.comm	OPENSSL_armcap_P,4,4
#endif
