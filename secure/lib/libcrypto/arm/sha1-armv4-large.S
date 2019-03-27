/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from sha1-armv4-large.pl. */
#include "arm_arch.h"

.text
#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif

.globl	sha1_block_data_order
.type	sha1_block_data_order,%function

.align	5
sha1_block_data_order:
#if __ARM_MAX_ARCH__>=7
.Lsha1_block:
	adr	r3,.Lsha1_block
	ldr	r12,.LOPENSSL_armcap
	ldr	r12,[r3,r12]		@ OPENSSL_armcap_P
#ifdef	__APPLE__
	ldr	r12,[r12]
#endif
	tst	r12,#ARMV8_SHA1
	bne	.LARMv8
	tst	r12,#ARMV7_NEON
	bne	.LNEON
#endif
	stmdb	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
	add	r2,r1,r2,lsl#6	@ r2 to point at the end of r1
	ldmia	r0,{r3,r4,r5,r6,r7}
.Lloop:
	ldr	r8,.LK_00_19
	mov	r14,sp
	sub	sp,sp,#15*4
	mov	r5,r5,ror#30
	mov	r6,r6,ror#30
	mov	r7,r7,ror#30		@ [6]
.L_00_15:
#if __ARM_ARCH__<7
	ldrb	r10,[r1,#2]
	ldrb	r9,[r1,#3]
	ldrb	r11,[r1,#1]
	add	r7,r8,r7,ror#2			@ E+=K_00_19
	ldrb	r12,[r1],#4
	orr	r9,r9,r10,lsl#8
	eor	r10,r5,r6			@ F_xx_xx
	orr	r9,r9,r11,lsl#16
	add	r7,r7,r3,ror#27			@ E+=ROR(A,27)
	orr	r9,r9,r12,lsl#24
#else
	ldr	r9,[r1],#4			@ handles unaligned
	add	r7,r8,r7,ror#2			@ E+=K_00_19
	eor	r10,r5,r6			@ F_xx_xx
	add	r7,r7,r3,ror#27			@ E+=ROR(A,27)
#ifdef __ARMEL__
	rev	r9,r9				@ byte swap
#endif
#endif
	and	r10,r4,r10,ror#2
	add	r7,r7,r9			@ E+=X[i]
	eor	r10,r10,r6,ror#2		@ F_00_19(B,C,D)
	str	r9,[r14,#-4]!
	add	r7,r7,r10			@ E+=F_00_19(B,C,D)
#if __ARM_ARCH__<7
	ldrb	r10,[r1,#2]
	ldrb	r9,[r1,#3]
	ldrb	r11,[r1,#1]
	add	r6,r8,r6,ror#2			@ E+=K_00_19
	ldrb	r12,[r1],#4
	orr	r9,r9,r10,lsl#8
	eor	r10,r4,r5			@ F_xx_xx
	orr	r9,r9,r11,lsl#16
	add	r6,r6,r7,ror#27			@ E+=ROR(A,27)
	orr	r9,r9,r12,lsl#24
#else
	ldr	r9,[r1],#4			@ handles unaligned
	add	r6,r8,r6,ror#2			@ E+=K_00_19
	eor	r10,r4,r5			@ F_xx_xx
	add	r6,r6,r7,ror#27			@ E+=ROR(A,27)
#ifdef __ARMEL__
	rev	r9,r9				@ byte swap
#endif
#endif
	and	r10,r3,r10,ror#2
	add	r6,r6,r9			@ E+=X[i]
	eor	r10,r10,r5,ror#2		@ F_00_19(B,C,D)
	str	r9,[r14,#-4]!
	add	r6,r6,r10			@ E+=F_00_19(B,C,D)
#if __ARM_ARCH__<7
	ldrb	r10,[r1,#2]
	ldrb	r9,[r1,#3]
	ldrb	r11,[r1,#1]
	add	r5,r8,r5,ror#2			@ E+=K_00_19
	ldrb	r12,[r1],#4
	orr	r9,r9,r10,lsl#8
	eor	r10,r3,r4			@ F_xx_xx
	orr	r9,r9,r11,lsl#16
	add	r5,r5,r6,ror#27			@ E+=ROR(A,27)
	orr	r9,r9,r12,lsl#24
#else
	ldr	r9,[r1],#4			@ handles unaligned
	add	r5,r8,r5,ror#2			@ E+=K_00_19
	eor	r10,r3,r4			@ F_xx_xx
	add	r5,r5,r6,ror#27			@ E+=ROR(A,27)
#ifdef __ARMEL__
	rev	r9,r9				@ byte swap
#endif
#endif
	and	r10,r7,r10,ror#2
	add	r5,r5,r9			@ E+=X[i]
	eor	r10,r10,r4,ror#2		@ F_00_19(B,C,D)
	str	r9,[r14,#-4]!
	add	r5,r5,r10			@ E+=F_00_19(B,C,D)
#if __ARM_ARCH__<7
	ldrb	r10,[r1,#2]
	ldrb	r9,[r1,#3]
	ldrb	r11,[r1,#1]
	add	r4,r8,r4,ror#2			@ E+=K_00_19
	ldrb	r12,[r1],#4
	orr	r9,r9,r10,lsl#8
	eor	r10,r7,r3			@ F_xx_xx
	orr	r9,r9,r11,lsl#16
	add	r4,r4,r5,ror#27			@ E+=ROR(A,27)
	orr	r9,r9,r12,lsl#24
#else
	ldr	r9,[r1],#4			@ handles unaligned
	add	r4,r8,r4,ror#2			@ E+=K_00_19
	eor	r10,r7,r3			@ F_xx_xx
	add	r4,r4,r5,ror#27			@ E+=ROR(A,27)
#ifdef __ARMEL__
	rev	r9,r9				@ byte swap
#endif
#endif
	and	r10,r6,r10,ror#2
	add	r4,r4,r9			@ E+=X[i]
	eor	r10,r10,r3,ror#2		@ F_00_19(B,C,D)
	str	r9,[r14,#-4]!
	add	r4,r4,r10			@ E+=F_00_19(B,C,D)
#if __ARM_ARCH__<7
	ldrb	r10,[r1,#2]
	ldrb	r9,[r1,#3]
	ldrb	r11,[r1,#1]
	add	r3,r8,r3,ror#2			@ E+=K_00_19
	ldrb	r12,[r1],#4
	orr	r9,r9,r10,lsl#8
	eor	r10,r6,r7			@ F_xx_xx
	orr	r9,r9,r11,lsl#16
	add	r3,r3,r4,ror#27			@ E+=ROR(A,27)
	orr	r9,r9,r12,lsl#24
#else
	ldr	r9,[r1],#4			@ handles unaligned
	add	r3,r8,r3,ror#2			@ E+=K_00_19
	eor	r10,r6,r7			@ F_xx_xx
	add	r3,r3,r4,ror#27			@ E+=ROR(A,27)
#ifdef __ARMEL__
	rev	r9,r9				@ byte swap
#endif
#endif
	and	r10,r5,r10,ror#2
	add	r3,r3,r9			@ E+=X[i]
	eor	r10,r10,r7,ror#2		@ F_00_19(B,C,D)
	str	r9,[r14,#-4]!
	add	r3,r3,r10			@ E+=F_00_19(B,C,D)
#if defined(__thumb2__)
	mov	r12,sp
	teq	r14,r12
#else
	teq	r14,sp
#endif
	bne	.L_00_15		@ [((11+4)*5+2)*3]
	sub	sp,sp,#25*4
#if __ARM_ARCH__<7
	ldrb	r10,[r1,#2]
	ldrb	r9,[r1,#3]
	ldrb	r11,[r1,#1]
	add	r7,r8,r7,ror#2			@ E+=K_00_19
	ldrb	r12,[r1],#4
	orr	r9,r9,r10,lsl#8
	eor	r10,r5,r6			@ F_xx_xx
	orr	r9,r9,r11,lsl#16
	add	r7,r7,r3,ror#27			@ E+=ROR(A,27)
	orr	r9,r9,r12,lsl#24
#else
	ldr	r9,[r1],#4			@ handles unaligned
	add	r7,r8,r7,ror#2			@ E+=K_00_19
	eor	r10,r5,r6			@ F_xx_xx
	add	r7,r7,r3,ror#27			@ E+=ROR(A,27)
#ifdef __ARMEL__
	rev	r9,r9				@ byte swap
#endif
#endif
	and	r10,r4,r10,ror#2
	add	r7,r7,r9			@ E+=X[i]
	eor	r10,r10,r6,ror#2		@ F_00_19(B,C,D)
	str	r9,[r14,#-4]!
	add	r7,r7,r10			@ E+=F_00_19(B,C,D)
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r6,r8,r6,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r4,r5			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r6,r6,r7,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r3,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r6,r6,r9			@ E+=X[i]
	eor	r10,r10,r5,ror#2		@ F_00_19(B,C,D)
	add	r6,r6,r10			@ E+=F_00_19(B,C,D)
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r5,r8,r5,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r3,r4			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r5,r5,r6,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r7,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r5,r5,r9			@ E+=X[i]
	eor	r10,r10,r4,ror#2		@ F_00_19(B,C,D)
	add	r5,r5,r10			@ E+=F_00_19(B,C,D)
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r4,r8,r4,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r7,r3			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r4,r4,r5,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r6,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r4,r4,r9			@ E+=X[i]
	eor	r10,r10,r3,ror#2		@ F_00_19(B,C,D)
	add	r4,r4,r10			@ E+=F_00_19(B,C,D)
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r3,r8,r3,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r6,r7			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r3,r3,r4,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r5,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r3,r3,r9			@ E+=X[i]
	eor	r10,r10,r7,ror#2		@ F_00_19(B,C,D)
	add	r3,r3,r10			@ E+=F_00_19(B,C,D)

	ldr	r8,.LK_20_39		@ [+15+16*4]
	cmn	sp,#0			@ [+3], clear carry to denote 20_39
.L_20_39_or_60_79:
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r7,r8,r7,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r5,r6			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r7,r7,r3,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	eor	r10,r4,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r7,r7,r9			@ E+=X[i]
	add	r7,r7,r10			@ E+=F_20_39(B,C,D)
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r6,r8,r6,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r4,r5			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r6,r6,r7,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	eor	r10,r3,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r6,r6,r9			@ E+=X[i]
	add	r6,r6,r10			@ E+=F_20_39(B,C,D)
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r5,r8,r5,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r3,r4			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r5,r5,r6,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	eor	r10,r7,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r5,r5,r9			@ E+=X[i]
	add	r5,r5,r10			@ E+=F_20_39(B,C,D)
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r4,r8,r4,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r7,r3			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r4,r4,r5,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	eor	r10,r6,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r4,r4,r9			@ E+=X[i]
	add	r4,r4,r10			@ E+=F_20_39(B,C,D)
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r3,r8,r3,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r6,r7			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r3,r3,r4,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	eor	r10,r5,r10,ror#2					@ F_xx_xx
						@ F_xx_xx
	add	r3,r3,r9			@ E+=X[i]
	add	r3,r3,r10			@ E+=F_20_39(B,C,D)
#if defined(__thumb2__)
	mov	r12,sp
	teq	r14,r12
#else
	teq	r14,sp			@ preserve carry
#endif
	bne	.L_20_39_or_60_79	@ [+((12+3)*5+2)*4]
	bcs	.L_done			@ [+((12+3)*5+2)*4], spare 300 bytes

	ldr	r8,.LK_40_59
	sub	sp,sp,#20*4		@ [+2]
.L_40_59:
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r7,r8,r7,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r5,r6			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r7,r7,r3,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r4,r10,ror#2					@ F_xx_xx
	and	r11,r5,r6					@ F_xx_xx
	add	r7,r7,r9			@ E+=X[i]
	add	r7,r7,r10			@ E+=F_40_59(B,C,D)
	add	r7,r7,r11,ror#2
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r6,r8,r6,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r4,r5			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r6,r6,r7,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r3,r10,ror#2					@ F_xx_xx
	and	r11,r4,r5					@ F_xx_xx
	add	r6,r6,r9			@ E+=X[i]
	add	r6,r6,r10			@ E+=F_40_59(B,C,D)
	add	r6,r6,r11,ror#2
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r5,r8,r5,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r3,r4			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r5,r5,r6,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r7,r10,ror#2					@ F_xx_xx
	and	r11,r3,r4					@ F_xx_xx
	add	r5,r5,r9			@ E+=X[i]
	add	r5,r5,r10			@ E+=F_40_59(B,C,D)
	add	r5,r5,r11,ror#2
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r4,r8,r4,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r7,r3			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r4,r4,r5,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r6,r10,ror#2					@ F_xx_xx
	and	r11,r7,r3					@ F_xx_xx
	add	r4,r4,r9			@ E+=X[i]
	add	r4,r4,r10			@ E+=F_40_59(B,C,D)
	add	r4,r4,r11,ror#2
	ldr	r9,[r14,#15*4]
	ldr	r10,[r14,#13*4]
	ldr	r11,[r14,#7*4]
	add	r3,r8,r3,ror#2			@ E+=K_xx_xx
	ldr	r12,[r14,#2*4]
	eor	r9,r9,r10
	eor	r11,r11,r12			@ 1 cycle stall
	eor	r10,r6,r7			@ F_xx_xx
	mov	r9,r9,ror#31
	add	r3,r3,r4,ror#27			@ E+=ROR(A,27)
	eor	r9,r9,r11,ror#31
	str	r9,[r14,#-4]!
	and	r10,r5,r10,ror#2					@ F_xx_xx
	and	r11,r6,r7					@ F_xx_xx
	add	r3,r3,r9			@ E+=X[i]
	add	r3,r3,r10			@ E+=F_40_59(B,C,D)
	add	r3,r3,r11,ror#2
#if defined(__thumb2__)
	mov	r12,sp
	teq	r14,r12
#else
	teq	r14,sp
#endif
	bne	.L_40_59		@ [+((12+5)*5+2)*4]

	ldr	r8,.LK_60_79
	sub	sp,sp,#20*4
	cmp	sp,#0			@ set carry to denote 60_79
	b	.L_20_39_or_60_79	@ [+4], spare 300 bytes
.L_done:
	add	sp,sp,#80*4		@ "deallocate" stack frame
	ldmia	r0,{r8,r9,r10,r11,r12}
	add	r3,r8,r3
	add	r4,r9,r4
	add	r5,r10,r5,ror#2
	add	r6,r11,r6,ror#2
	add	r7,r12,r7,ror#2
	stmia	r0,{r3,r4,r5,r6,r7}
	teq	r1,r2
	bne	.Lloop			@ [+18], total 1307

#if __ARM_ARCH__>=5
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,r12,pc}
#else
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
.size	sha1_block_data_order,.-sha1_block_data_order

.align	5
.LK_00_19:.word	0x5a827999
.LK_20_39:.word	0x6ed9eba1
.LK_40_59:.word	0x8f1bbcdc
.LK_60_79:.word	0xca62c1d6
#if __ARM_MAX_ARCH__>=7
.LOPENSSL_armcap:
.word	OPENSSL_armcap_P-.Lsha1_block
#endif
.byte	83,72,65,49,32,98,108,111,99,107,32,116,114,97,110,115,102,111,114,109,32,102,111,114,32,65,82,77,118,52,47,78,69,79,78,47,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	5
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.type	sha1_block_data_order_neon,%function
.align	4
sha1_block_data_order_neon:
.LNEON:
	stmdb	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
	add	r2,r1,r2,lsl#6	@ r2 to point at the end of r1
	@ dmb				@ errata #451034 on early Cortex A8
	@ vstmdb	sp!,{d8-d15}	@ ABI specification says so
	mov	r14,sp
	sub	r12,sp,#64
	adr	r8,.LK_00_19
	bic	r12,r12,#15		@ align for 128-bit stores

	ldmia	r0,{r3,r4,r5,r6,r7}	@ load context
	mov	sp,r12		@ alloca

	vld1.8	{q0,q1},[r1]!	@ handles unaligned
	veor	q15,q15,q15
	vld1.8	{q2,q3},[r1]!
	vld1.32	{d28[],d29[]},[r8,:32]!	@ load K_00_19
	vrev32.8	q0,q0		@ yes, even on
	vrev32.8	q1,q1		@ big-endian...
	vrev32.8	q2,q2
	vadd.i32	q8,q0,q14
	vrev32.8	q3,q3
	vadd.i32	q9,q1,q14
	vst1.32	{q8},[r12,:128]!
	vadd.i32	q10,q2,q14
	vst1.32	{q9},[r12,:128]!
	vst1.32	{q10},[r12,:128]!
	ldr	r9,[sp]			@ big RAW stall

.Loop_neon:
	vext.8	q8,q0,q1,#8
	bic	r10,r6,r4
	add	r7,r7,r9
	and	r11,r5,r4
	vadd.i32	q13,q3,q14
	ldr	r9,[sp,#4]
	add	r7,r7,r3,ror#27
	vext.8	q12,q3,q15,#4
	eor	r11,r11,r10
	mov	r4,r4,ror#2
	add	r7,r7,r11
	veor	q8,q8,q0
	bic	r10,r5,r3
	add	r6,r6,r9
	veor	q12,q12,q2
	and	r11,r4,r3
	ldr	r9,[sp,#8]
	veor	q12,q12,q8
	add	r6,r6,r7,ror#27
	eor	r11,r11,r10
	vst1.32	{q13},[r12,:128]!
	sub	r12,r12,#64
	mov	r3,r3,ror#2
	add	r6,r6,r11
	vext.8	q13,q15,q12,#4
	bic	r10,r4,r7
	add	r5,r5,r9
	vadd.i32	q8,q12,q12
	and	r11,r3,r7
	ldr	r9,[sp,#12]
	vsri.32	q8,q12,#31
	add	r5,r5,r6,ror#27
	eor	r11,r11,r10
	mov	r7,r7,ror#2
	vshr.u32	q12,q13,#30
	add	r5,r5,r11
	bic	r10,r3,r6
	vshl.u32	q13,q13,#2
	add	r4,r4,r9
	and	r11,r7,r6
	veor	q8,q8,q12
	ldr	r9,[sp,#16]
	add	r4,r4,r5,ror#27
	veor	q8,q8,q13
	eor	r11,r11,r10
	mov	r6,r6,ror#2
	add	r4,r4,r11
	vext.8	q9,q1,q2,#8
	bic	r10,r7,r5
	add	r3,r3,r9
	and	r11,r6,r5
	vadd.i32	q13,q8,q14
	ldr	r9,[sp,#20]
	vld1.32	{d28[],d29[]},[r8,:32]!
	add	r3,r3,r4,ror#27
	vext.8	q12,q8,q15,#4
	eor	r11,r11,r10
	mov	r5,r5,ror#2
	add	r3,r3,r11
	veor	q9,q9,q1
	bic	r10,r6,r4
	add	r7,r7,r9
	veor	q12,q12,q3
	and	r11,r5,r4
	ldr	r9,[sp,#24]
	veor	q12,q12,q9
	add	r7,r7,r3,ror#27
	eor	r11,r11,r10
	vst1.32	{q13},[r12,:128]!
	mov	r4,r4,ror#2
	add	r7,r7,r11
	vext.8	q13,q15,q12,#4
	bic	r10,r5,r3
	add	r6,r6,r9
	vadd.i32	q9,q12,q12
	and	r11,r4,r3
	ldr	r9,[sp,#28]
	vsri.32	q9,q12,#31
	add	r6,r6,r7,ror#27
	eor	r11,r11,r10
	mov	r3,r3,ror#2
	vshr.u32	q12,q13,#30
	add	r6,r6,r11
	bic	r10,r4,r7
	vshl.u32	q13,q13,#2
	add	r5,r5,r9
	and	r11,r3,r7
	veor	q9,q9,q12
	ldr	r9,[sp,#32]
	add	r5,r5,r6,ror#27
	veor	q9,q9,q13
	eor	r11,r11,r10
	mov	r7,r7,ror#2
	add	r5,r5,r11
	vext.8	q10,q2,q3,#8
	bic	r10,r3,r6
	add	r4,r4,r9
	and	r11,r7,r6
	vadd.i32	q13,q9,q14
	ldr	r9,[sp,#36]
	add	r4,r4,r5,ror#27
	vext.8	q12,q9,q15,#4
	eor	r11,r11,r10
	mov	r6,r6,ror#2
	add	r4,r4,r11
	veor	q10,q10,q2
	bic	r10,r7,r5
	add	r3,r3,r9
	veor	q12,q12,q8
	and	r11,r6,r5
	ldr	r9,[sp,#40]
	veor	q12,q12,q10
	add	r3,r3,r4,ror#27
	eor	r11,r11,r10
	vst1.32	{q13},[r12,:128]!
	mov	r5,r5,ror#2
	add	r3,r3,r11
	vext.8	q13,q15,q12,#4
	bic	r10,r6,r4
	add	r7,r7,r9
	vadd.i32	q10,q12,q12
	and	r11,r5,r4
	ldr	r9,[sp,#44]
	vsri.32	q10,q12,#31
	add	r7,r7,r3,ror#27
	eor	r11,r11,r10
	mov	r4,r4,ror#2
	vshr.u32	q12,q13,#30
	add	r7,r7,r11
	bic	r10,r5,r3
	vshl.u32	q13,q13,#2
	add	r6,r6,r9
	and	r11,r4,r3
	veor	q10,q10,q12
	ldr	r9,[sp,#48]
	add	r6,r6,r7,ror#27
	veor	q10,q10,q13
	eor	r11,r11,r10
	mov	r3,r3,ror#2
	add	r6,r6,r11
	vext.8	q11,q3,q8,#8
	bic	r10,r4,r7
	add	r5,r5,r9
	and	r11,r3,r7
	vadd.i32	q13,q10,q14
	ldr	r9,[sp,#52]
	add	r5,r5,r6,ror#27
	vext.8	q12,q10,q15,#4
	eor	r11,r11,r10
	mov	r7,r7,ror#2
	add	r5,r5,r11
	veor	q11,q11,q3
	bic	r10,r3,r6
	add	r4,r4,r9
	veor	q12,q12,q9
	and	r11,r7,r6
	ldr	r9,[sp,#56]
	veor	q12,q12,q11
	add	r4,r4,r5,ror#27
	eor	r11,r11,r10
	vst1.32	{q13},[r12,:128]!
	mov	r6,r6,ror#2
	add	r4,r4,r11
	vext.8	q13,q15,q12,#4
	bic	r10,r7,r5
	add	r3,r3,r9
	vadd.i32	q11,q12,q12
	and	r11,r6,r5
	ldr	r9,[sp,#60]
	vsri.32	q11,q12,#31
	add	r3,r3,r4,ror#27
	eor	r11,r11,r10
	mov	r5,r5,ror#2
	vshr.u32	q12,q13,#30
	add	r3,r3,r11
	bic	r10,r6,r4
	vshl.u32	q13,q13,#2
	add	r7,r7,r9
	and	r11,r5,r4
	veor	q11,q11,q12
	ldr	r9,[sp,#0]
	add	r7,r7,r3,ror#27
	veor	q11,q11,q13
	eor	r11,r11,r10
	mov	r4,r4,ror#2
	add	r7,r7,r11
	vext.8	q12,q10,q11,#8
	bic	r10,r5,r3
	add	r6,r6,r9
	and	r11,r4,r3
	veor	q0,q0,q8
	ldr	r9,[sp,#4]
	add	r6,r6,r7,ror#27
	veor	q0,q0,q1
	eor	r11,r11,r10
	mov	r3,r3,ror#2
	vadd.i32	q13,q11,q14
	add	r6,r6,r11
	bic	r10,r4,r7
	veor	q12,q12,q0
	add	r5,r5,r9
	and	r11,r3,r7
	vshr.u32	q0,q12,#30
	ldr	r9,[sp,#8]
	add	r5,r5,r6,ror#27
	vst1.32	{q13},[r12,:128]!
	sub	r12,r12,#64
	eor	r11,r11,r10
	mov	r7,r7,ror#2
	vsli.32	q0,q12,#2
	add	r5,r5,r11
	bic	r10,r3,r6
	add	r4,r4,r9
	and	r11,r7,r6
	ldr	r9,[sp,#12]
	add	r4,r4,r5,ror#27
	eor	r11,r11,r10
	mov	r6,r6,ror#2
	add	r4,r4,r11
	bic	r10,r7,r5
	add	r3,r3,r9
	and	r11,r6,r5
	ldr	r9,[sp,#16]
	add	r3,r3,r4,ror#27
	eor	r11,r11,r10
	mov	r5,r5,ror#2
	add	r3,r3,r11
	vext.8	q12,q11,q0,#8
	eor	r10,r4,r6
	add	r7,r7,r9
	ldr	r9,[sp,#20]
	veor	q1,q1,q9
	eor	r11,r10,r5
	add	r7,r7,r3,ror#27
	veor	q1,q1,q2
	mov	r4,r4,ror#2
	add	r7,r7,r11
	vadd.i32	q13,q0,q14
	eor	r10,r3,r5
	add	r6,r6,r9
	veor	q12,q12,q1
	ldr	r9,[sp,#24]
	eor	r11,r10,r4
	vshr.u32	q1,q12,#30
	add	r6,r6,r7,ror#27
	mov	r3,r3,ror#2
	vst1.32	{q13},[r12,:128]!
	add	r6,r6,r11
	eor	r10,r7,r4
	vsli.32	q1,q12,#2
	add	r5,r5,r9
	ldr	r9,[sp,#28]
	eor	r11,r10,r3
	add	r5,r5,r6,ror#27
	mov	r7,r7,ror#2
	add	r5,r5,r11
	eor	r10,r6,r3
	add	r4,r4,r9
	ldr	r9,[sp,#32]
	eor	r11,r10,r7
	add	r4,r4,r5,ror#27
	mov	r6,r6,ror#2
	add	r4,r4,r11
	vext.8	q12,q0,q1,#8
	eor	r10,r5,r7
	add	r3,r3,r9
	ldr	r9,[sp,#36]
	veor	q2,q2,q10
	eor	r11,r10,r6
	add	r3,r3,r4,ror#27
	veor	q2,q2,q3
	mov	r5,r5,ror#2
	add	r3,r3,r11
	vadd.i32	q13,q1,q14
	eor	r10,r4,r6
	vld1.32	{d28[],d29[]},[r8,:32]!
	add	r7,r7,r9
	veor	q12,q12,q2
	ldr	r9,[sp,#40]
	eor	r11,r10,r5
	vshr.u32	q2,q12,#30
	add	r7,r7,r3,ror#27
	mov	r4,r4,ror#2
	vst1.32	{q13},[r12,:128]!
	add	r7,r7,r11
	eor	r10,r3,r5
	vsli.32	q2,q12,#2
	add	r6,r6,r9
	ldr	r9,[sp,#44]
	eor	r11,r10,r4
	add	r6,r6,r7,ror#27
	mov	r3,r3,ror#2
	add	r6,r6,r11
	eor	r10,r7,r4
	add	r5,r5,r9
	ldr	r9,[sp,#48]
	eor	r11,r10,r3
	add	r5,r5,r6,ror#27
	mov	r7,r7,ror#2
	add	r5,r5,r11
	vext.8	q12,q1,q2,#8
	eor	r10,r6,r3
	add	r4,r4,r9
	ldr	r9,[sp,#52]
	veor	q3,q3,q11
	eor	r11,r10,r7
	add	r4,r4,r5,ror#27
	veor	q3,q3,q8
	mov	r6,r6,ror#2
	add	r4,r4,r11
	vadd.i32	q13,q2,q14
	eor	r10,r5,r7
	add	r3,r3,r9
	veor	q12,q12,q3
	ldr	r9,[sp,#56]
	eor	r11,r10,r6
	vshr.u32	q3,q12,#30
	add	r3,r3,r4,ror#27
	mov	r5,r5,ror#2
	vst1.32	{q13},[r12,:128]!
	add	r3,r3,r11
	eor	r10,r4,r6
	vsli.32	q3,q12,#2
	add	r7,r7,r9
	ldr	r9,[sp,#60]
	eor	r11,r10,r5
	add	r7,r7,r3,ror#27
	mov	r4,r4,ror#2
	add	r7,r7,r11
	eor	r10,r3,r5
	add	r6,r6,r9
	ldr	r9,[sp,#0]
	eor	r11,r10,r4
	add	r6,r6,r7,ror#27
	mov	r3,r3,ror#2
	add	r6,r6,r11
	vext.8	q12,q2,q3,#8
	eor	r10,r7,r4
	add	r5,r5,r9
	ldr	r9,[sp,#4]
	veor	q8,q8,q0
	eor	r11,r10,r3
	add	r5,r5,r6,ror#27
	veor	q8,q8,q9
	mov	r7,r7,ror#2
	add	r5,r5,r11
	vadd.i32	q13,q3,q14
	eor	r10,r6,r3
	add	r4,r4,r9
	veor	q12,q12,q8
	ldr	r9,[sp,#8]
	eor	r11,r10,r7
	vshr.u32	q8,q12,#30
	add	r4,r4,r5,ror#27
	mov	r6,r6,ror#2
	vst1.32	{q13},[r12,:128]!
	sub	r12,r12,#64
	add	r4,r4,r11
	eor	r10,r5,r7
	vsli.32	q8,q12,#2
	add	r3,r3,r9
	ldr	r9,[sp,#12]
	eor	r11,r10,r6
	add	r3,r3,r4,ror#27
	mov	r5,r5,ror#2
	add	r3,r3,r11
	eor	r10,r4,r6
	add	r7,r7,r9
	ldr	r9,[sp,#16]
	eor	r11,r10,r5
	add	r7,r7,r3,ror#27
	mov	r4,r4,ror#2
	add	r7,r7,r11
	vext.8	q12,q3,q8,#8
	eor	r10,r3,r5
	add	r6,r6,r9
	ldr	r9,[sp,#20]
	veor	q9,q9,q1
	eor	r11,r10,r4
	add	r6,r6,r7,ror#27
	veor	q9,q9,q10
	mov	r3,r3,ror#2
	add	r6,r6,r11
	vadd.i32	q13,q8,q14
	eor	r10,r7,r4
	add	r5,r5,r9
	veor	q12,q12,q9
	ldr	r9,[sp,#24]
	eor	r11,r10,r3
	vshr.u32	q9,q12,#30
	add	r5,r5,r6,ror#27
	mov	r7,r7,ror#2
	vst1.32	{q13},[r12,:128]!
	add	r5,r5,r11
	eor	r10,r6,r3
	vsli.32	q9,q12,#2
	add	r4,r4,r9
	ldr	r9,[sp,#28]
	eor	r11,r10,r7
	add	r4,r4,r5,ror#27
	mov	r6,r6,ror#2
	add	r4,r4,r11
	eor	r10,r5,r7
	add	r3,r3,r9
	ldr	r9,[sp,#32]
	eor	r11,r10,r6
	add	r3,r3,r4,ror#27
	mov	r5,r5,ror#2
	add	r3,r3,r11
	vext.8	q12,q8,q9,#8
	add	r7,r7,r9
	and	r10,r5,r6
	ldr	r9,[sp,#36]
	veor	q10,q10,q2
	add	r7,r7,r3,ror#27
	eor	r11,r5,r6
	veor	q10,q10,q11
	add	r7,r7,r10
	and	r11,r11,r4
	vadd.i32	q13,q9,q14
	mov	r4,r4,ror#2
	add	r7,r7,r11
	veor	q12,q12,q10
	add	r6,r6,r9
	and	r10,r4,r5
	vshr.u32	q10,q12,#30
	ldr	r9,[sp,#40]
	add	r6,r6,r7,ror#27
	vst1.32	{q13},[r12,:128]!
	eor	r11,r4,r5
	add	r6,r6,r10
	vsli.32	q10,q12,#2
	and	r11,r11,r3
	mov	r3,r3,ror#2
	add	r6,r6,r11
	add	r5,r5,r9
	and	r10,r3,r4
	ldr	r9,[sp,#44]
	add	r5,r5,r6,ror#27
	eor	r11,r3,r4
	add	r5,r5,r10
	and	r11,r11,r7
	mov	r7,r7,ror#2
	add	r5,r5,r11
	add	r4,r4,r9
	and	r10,r7,r3
	ldr	r9,[sp,#48]
	add	r4,r4,r5,ror#27
	eor	r11,r7,r3
	add	r4,r4,r10
	and	r11,r11,r6
	mov	r6,r6,ror#2
	add	r4,r4,r11
	vext.8	q12,q9,q10,#8
	add	r3,r3,r9
	and	r10,r6,r7
	ldr	r9,[sp,#52]
	veor	q11,q11,q3
	add	r3,r3,r4,ror#27
	eor	r11,r6,r7
	veor	q11,q11,q0
	add	r3,r3,r10
	and	r11,r11,r5
	vadd.i32	q13,q10,q14
	mov	r5,r5,ror#2
	vld1.32	{d28[],d29[]},[r8,:32]!
	add	r3,r3,r11
	veor	q12,q12,q11
	add	r7,r7,r9
	and	r10,r5,r6
	vshr.u32	q11,q12,#30
	ldr	r9,[sp,#56]
	add	r7,r7,r3,ror#27
	vst1.32	{q13},[r12,:128]!
	eor	r11,r5,r6
	add	r7,r7,r10
	vsli.32	q11,q12,#2
	and	r11,r11,r4
	mov	r4,r4,ror#2
	add	r7,r7,r11
	add	r6,r6,r9
	and	r10,r4,r5
	ldr	r9,[sp,#60]
	add	r6,r6,r7,ror#27
	eor	r11,r4,r5
	add	r6,r6,r10
	and	r11,r11,r3
	mov	r3,r3,ror#2
	add	r6,r6,r11
	add	r5,r5,r9
	and	r10,r3,r4
	ldr	r9,[sp,#0]
	add	r5,r5,r6,ror#27
	eor	r11,r3,r4
	add	r5,r5,r10
	and	r11,r11,r7
	mov	r7,r7,ror#2
	add	r5,r5,r11
	vext.8	q12,q10,q11,#8
	add	r4,r4,r9
	and	r10,r7,r3
	ldr	r9,[sp,#4]
	veor	q0,q0,q8
	add	r4,r4,r5,ror#27
	eor	r11,r7,r3
	veor	q0,q0,q1
	add	r4,r4,r10
	and	r11,r11,r6
	vadd.i32	q13,q11,q14
	mov	r6,r6,ror#2
	add	r4,r4,r11
	veor	q12,q12,q0
	add	r3,r3,r9
	and	r10,r6,r7
	vshr.u32	q0,q12,#30
	ldr	r9,[sp,#8]
	add	r3,r3,r4,ror#27
	vst1.32	{q13},[r12,:128]!
	sub	r12,r12,#64
	eor	r11,r6,r7
	add	r3,r3,r10
	vsli.32	q0,q12,#2
	and	r11,r11,r5
	mov	r5,r5,ror#2
	add	r3,r3,r11
	add	r7,r7,r9
	and	r10,r5,r6
	ldr	r9,[sp,#12]
	add	r7,r7,r3,ror#27
	eor	r11,r5,r6
	add	r7,r7,r10
	and	r11,r11,r4
	mov	r4,r4,ror#2
	add	r7,r7,r11
	add	r6,r6,r9
	and	r10,r4,r5
	ldr	r9,[sp,#16]
	add	r6,r6,r7,ror#27
	eor	r11,r4,r5
	add	r6,r6,r10
	and	r11,r11,r3
	mov	r3,r3,ror#2
	add	r6,r6,r11
	vext.8	q12,q11,q0,#8
	add	r5,r5,r9
	and	r10,r3,r4
	ldr	r9,[sp,#20]
	veor	q1,q1,q9
	add	r5,r5,r6,ror#27
	eor	r11,r3,r4
	veor	q1,q1,q2
	add	r5,r5,r10
	and	r11,r11,r7
	vadd.i32	q13,q0,q14
	mov	r7,r7,ror#2
	add	r5,r5,r11
	veor	q12,q12,q1
	add	r4,r4,r9
	and	r10,r7,r3
	vshr.u32	q1,q12,#30
	ldr	r9,[sp,#24]
	add	r4,r4,r5,ror#27
	vst1.32	{q13},[r12,:128]!
	eor	r11,r7,r3
	add	r4,r4,r10
	vsli.32	q1,q12,#2
	and	r11,r11,r6
	mov	r6,r6,ror#2
	add	r4,r4,r11
	add	r3,r3,r9
	and	r10,r6,r7
	ldr	r9,[sp,#28]
	add	r3,r3,r4,ror#27
	eor	r11,r6,r7
	add	r3,r3,r10
	and	r11,r11,r5
	mov	r5,r5,ror#2
	add	r3,r3,r11
	add	r7,r7,r9
	and	r10,r5,r6
	ldr	r9,[sp,#32]
	add	r7,r7,r3,ror#27
	eor	r11,r5,r6
	add	r7,r7,r10
	and	r11,r11,r4
	mov	r4,r4,ror#2
	add	r7,r7,r11
	vext.8	q12,q0,q1,#8
	add	r6,r6,r9
	and	r10,r4,r5
	ldr	r9,[sp,#36]
	veor	q2,q2,q10
	add	r6,r6,r7,ror#27
	eor	r11,r4,r5
	veor	q2,q2,q3
	add	r6,r6,r10
	and	r11,r11,r3
	vadd.i32	q13,q1,q14
	mov	r3,r3,ror#2
	add	r6,r6,r11
	veor	q12,q12,q2
	add	r5,r5,r9
	and	r10,r3,r4
	vshr.u32	q2,q12,#30
	ldr	r9,[sp,#40]
	add	r5,r5,r6,ror#27
	vst1.32	{q13},[r12,:128]!
	eor	r11,r3,r4
	add	r5,r5,r10
	vsli.32	q2,q12,#2
	and	r11,r11,r7
	mov	r7,r7,ror#2
	add	r5,r5,r11
	add	r4,r4,r9
	and	r10,r7,r3
	ldr	r9,[sp,#44]
	add	r4,r4,r5,ror#27
	eor	r11,r7,r3
	add	r4,r4,r10
	and	r11,r11,r6
	mov	r6,r6,ror#2
	add	r4,r4,r11
	add	r3,r3,r9
	and	r10,r6,r7
	ldr	r9,[sp,#48]
	add	r3,r3,r4,ror#27
	eor	r11,r6,r7
	add	r3,r3,r10
	and	r11,r11,r5
	mov	r5,r5,ror#2
	add	r3,r3,r11
	vext.8	q12,q1,q2,#8
	eor	r10,r4,r6
	add	r7,r7,r9
	ldr	r9,[sp,#52]
	veor	q3,q3,q11
	eor	r11,r10,r5
	add	r7,r7,r3,ror#27
	veor	q3,q3,q8
	mov	r4,r4,ror#2
	add	r7,r7,r11
	vadd.i32	q13,q2,q14
	eor	r10,r3,r5
	add	r6,r6,r9
	veor	q12,q12,q3
	ldr	r9,[sp,#56]
	eor	r11,r10,r4
	vshr.u32	q3,q12,#30
	add	r6,r6,r7,ror#27
	mov	r3,r3,ror#2
	vst1.32	{q13},[r12,:128]!
	add	r6,r6,r11
	eor	r10,r7,r4
	vsli.32	q3,q12,#2
	add	r5,r5,r9
	ldr	r9,[sp,#60]
	eor	r11,r10,r3
	add	r5,r5,r6,ror#27
	mov	r7,r7,ror#2
	add	r5,r5,r11
	eor	r10,r6,r3
	add	r4,r4,r9
	ldr	r9,[sp,#0]
	eor	r11,r10,r7
	add	r4,r4,r5,ror#27
	mov	r6,r6,ror#2
	add	r4,r4,r11
	vadd.i32	q13,q3,q14
	eor	r10,r5,r7
	add	r3,r3,r9
	vst1.32	{q13},[r12,:128]!
	sub	r12,r12,#64
	teq	r1,r2
	sub	r8,r8,#16
	it	eq
	subeq	r1,r1,#64
	vld1.8	{q0,q1},[r1]!
	ldr	r9,[sp,#4]
	eor	r11,r10,r6
	vld1.8	{q2,q3},[r1]!
	add	r3,r3,r4,ror#27
	mov	r5,r5,ror#2
	vld1.32	{d28[],d29[]},[r8,:32]!
	add	r3,r3,r11
	eor	r10,r4,r6
	vrev32.8	q0,q0
	add	r7,r7,r9
	ldr	r9,[sp,#8]
	eor	r11,r10,r5
	add	r7,r7,r3,ror#27
	mov	r4,r4,ror#2
	add	r7,r7,r11
	eor	r10,r3,r5
	add	r6,r6,r9
	ldr	r9,[sp,#12]
	eor	r11,r10,r4
	add	r6,r6,r7,ror#27
	mov	r3,r3,ror#2
	add	r6,r6,r11
	eor	r10,r7,r4
	add	r5,r5,r9
	ldr	r9,[sp,#16]
	eor	r11,r10,r3
	add	r5,r5,r6,ror#27
	mov	r7,r7,ror#2
	add	r5,r5,r11
	vrev32.8	q1,q1
	eor	r10,r6,r3
	add	r4,r4,r9
	vadd.i32	q8,q0,q14
	ldr	r9,[sp,#20]
	eor	r11,r10,r7
	vst1.32	{q8},[r12,:128]!
	add	r4,r4,r5,ror#27
	mov	r6,r6,ror#2
	add	r4,r4,r11
	eor	r10,r5,r7
	add	r3,r3,r9
	ldr	r9,[sp,#24]
	eor	r11,r10,r6
	add	r3,r3,r4,ror#27
	mov	r5,r5,ror#2
	add	r3,r3,r11
	eor	r10,r4,r6
	add	r7,r7,r9
	ldr	r9,[sp,#28]
	eor	r11,r10,r5
	add	r7,r7,r3,ror#27
	mov	r4,r4,ror#2
	add	r7,r7,r11
	eor	r10,r3,r5
	add	r6,r6,r9
	ldr	r9,[sp,#32]
	eor	r11,r10,r4
	add	r6,r6,r7,ror#27
	mov	r3,r3,ror#2
	add	r6,r6,r11
	vrev32.8	q2,q2
	eor	r10,r7,r4
	add	r5,r5,r9
	vadd.i32	q9,q1,q14
	ldr	r9,[sp,#36]
	eor	r11,r10,r3
	vst1.32	{q9},[r12,:128]!
	add	r5,r5,r6,ror#27
	mov	r7,r7,ror#2
	add	r5,r5,r11
	eor	r10,r6,r3
	add	r4,r4,r9
	ldr	r9,[sp,#40]
	eor	r11,r10,r7
	add	r4,r4,r5,ror#27
	mov	r6,r6,ror#2
	add	r4,r4,r11
	eor	r10,r5,r7
	add	r3,r3,r9
	ldr	r9,[sp,#44]
	eor	r11,r10,r6
	add	r3,r3,r4,ror#27
	mov	r5,r5,ror#2
	add	r3,r3,r11
	eor	r10,r4,r6
	add	r7,r7,r9
	ldr	r9,[sp,#48]
	eor	r11,r10,r5
	add	r7,r7,r3,ror#27
	mov	r4,r4,ror#2
	add	r7,r7,r11
	vrev32.8	q3,q3
	eor	r10,r3,r5
	add	r6,r6,r9
	vadd.i32	q10,q2,q14
	ldr	r9,[sp,#52]
	eor	r11,r10,r4
	vst1.32	{q10},[r12,:128]!
	add	r6,r6,r7,ror#27
	mov	r3,r3,ror#2
	add	r6,r6,r11
	eor	r10,r7,r4
	add	r5,r5,r9
	ldr	r9,[sp,#56]
	eor	r11,r10,r3
	add	r5,r5,r6,ror#27
	mov	r7,r7,ror#2
	add	r5,r5,r11
	eor	r10,r6,r3
	add	r4,r4,r9
	ldr	r9,[sp,#60]
	eor	r11,r10,r7
	add	r4,r4,r5,ror#27
	mov	r6,r6,ror#2
	add	r4,r4,r11
	eor	r10,r5,r7
	add	r3,r3,r9
	eor	r11,r10,r6
	add	r3,r3,r4,ror#27
	mov	r5,r5,ror#2
	add	r3,r3,r11
	ldmia	r0,{r9,r10,r11,r12}	@ accumulate context
	add	r3,r3,r9
	ldr	r9,[r0,#16]
	add	r4,r4,r10
	add	r5,r5,r11
	add	r6,r6,r12
	it	eq
	moveq	sp,r14
	add	r7,r7,r9
	it	ne
	ldrne	r9,[sp]
	stmia	r0,{r3,r4,r5,r6,r7}
	itt	ne
	addne	r12,sp,#3*16
	bne	.Loop_neon

	@ vldmia	sp!,{d8-d15}
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,r12,pc}
.size	sha1_block_data_order_neon,.-sha1_block_data_order_neon
#endif
#if __ARM_MAX_ARCH__>=7

# if defined(__thumb2__)
#  define INST(a,b,c,d)	.byte	c,d|0xf,a,b
# else
#  define INST(a,b,c,d)	.byte	a,b,c,d|0x10
# endif

.type	sha1_block_data_order_armv8,%function
.align	5
sha1_block_data_order_armv8:
.LARMv8:
	vstmdb	sp!,{d8,d9,d10,d11,d12,d13,d14,d15}		@ ABI specification says so

	veor	q1,q1,q1
	adr	r3,.LK_00_19
	vld1.32	{q0},[r0]!
	vld1.32	{d2[0]},[r0]
	sub	r0,r0,#16
	vld1.32	{d16[],d17[]},[r3,:32]!
	vld1.32	{d18[],d19[]},[r3,:32]!
	vld1.32	{d20[],d21[]},[r3,:32]!
	vld1.32	{d22[],d23[]},[r3,:32]

.Loop_v8:
	vld1.8	{q4,q5},[r1]!
	vld1.8	{q6,q7},[r1]!
	vrev32.8	q4,q4
	vrev32.8	q5,q5

	vadd.i32	q12,q8,q4
	vrev32.8	q6,q6
	vmov	q14,q0	@ offload
	subs	r2,r2,#1

	vadd.i32	q13,q8,q5
	vrev32.8	q7,q7
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 0
	INST(0x68,0x0c,0x02,0xe2)	@ sha1c q0,q1,q12
	vadd.i32	q12,q8,q6
	INST(0x4c,0x8c,0x3a,0xe2)	@ sha1su0 q4,q5,q6
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 1
	INST(0x6a,0x0c,0x06,0xe2)	@ sha1c q0,q3,q13
	vadd.i32	q13,q8,q7
	INST(0x8e,0x83,0xba,0xf3)	@ sha1su1 q4,q7
	INST(0x4e,0xac,0x3c,0xe2)	@ sha1su0 q5,q6,q7
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 2
	INST(0x68,0x0c,0x04,0xe2)	@ sha1c q0,q2,q12
	vadd.i32	q12,q8,q4
	INST(0x88,0xa3,0xba,0xf3)	@ sha1su1 q5,q4
	INST(0x48,0xcc,0x3e,0xe2)	@ sha1su0 q6,q7,q4
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 3
	INST(0x6a,0x0c,0x06,0xe2)	@ sha1c q0,q3,q13
	vadd.i32	q13,q9,q5
	INST(0x8a,0xc3,0xba,0xf3)	@ sha1su1 q6,q5
	INST(0x4a,0xec,0x38,0xe2)	@ sha1su0 q7,q4,q5
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 4
	INST(0x68,0x0c,0x04,0xe2)	@ sha1c q0,q2,q12
	vadd.i32	q12,q9,q6
	INST(0x8c,0xe3,0xba,0xf3)	@ sha1su1 q7,q6
	INST(0x4c,0x8c,0x3a,0xe2)	@ sha1su0 q4,q5,q6
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 5
	INST(0x6a,0x0c,0x16,0xe2)	@ sha1p q0,q3,q13
	vadd.i32	q13,q9,q7
	INST(0x8e,0x83,0xba,0xf3)	@ sha1su1 q4,q7
	INST(0x4e,0xac,0x3c,0xe2)	@ sha1su0 q5,q6,q7
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 6
	INST(0x68,0x0c,0x14,0xe2)	@ sha1p q0,q2,q12
	vadd.i32	q12,q9,q4
	INST(0x88,0xa3,0xba,0xf3)	@ sha1su1 q5,q4
	INST(0x48,0xcc,0x3e,0xe2)	@ sha1su0 q6,q7,q4
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 7
	INST(0x6a,0x0c,0x16,0xe2)	@ sha1p q0,q3,q13
	vadd.i32	q13,q9,q5
	INST(0x8a,0xc3,0xba,0xf3)	@ sha1su1 q6,q5
	INST(0x4a,0xec,0x38,0xe2)	@ sha1su0 q7,q4,q5
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 8
	INST(0x68,0x0c,0x14,0xe2)	@ sha1p q0,q2,q12
	vadd.i32	q12,q10,q6
	INST(0x8c,0xe3,0xba,0xf3)	@ sha1su1 q7,q6
	INST(0x4c,0x8c,0x3a,0xe2)	@ sha1su0 q4,q5,q6
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 9
	INST(0x6a,0x0c,0x16,0xe2)	@ sha1p q0,q3,q13
	vadd.i32	q13,q10,q7
	INST(0x8e,0x83,0xba,0xf3)	@ sha1su1 q4,q7
	INST(0x4e,0xac,0x3c,0xe2)	@ sha1su0 q5,q6,q7
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 10
	INST(0x68,0x0c,0x24,0xe2)	@ sha1m q0,q2,q12
	vadd.i32	q12,q10,q4
	INST(0x88,0xa3,0xba,0xf3)	@ sha1su1 q5,q4
	INST(0x48,0xcc,0x3e,0xe2)	@ sha1su0 q6,q7,q4
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 11
	INST(0x6a,0x0c,0x26,0xe2)	@ sha1m q0,q3,q13
	vadd.i32	q13,q10,q5
	INST(0x8a,0xc3,0xba,0xf3)	@ sha1su1 q6,q5
	INST(0x4a,0xec,0x38,0xe2)	@ sha1su0 q7,q4,q5
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 12
	INST(0x68,0x0c,0x24,0xe2)	@ sha1m q0,q2,q12
	vadd.i32	q12,q10,q6
	INST(0x8c,0xe3,0xba,0xf3)	@ sha1su1 q7,q6
	INST(0x4c,0x8c,0x3a,0xe2)	@ sha1su0 q4,q5,q6
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 13
	INST(0x6a,0x0c,0x26,0xe2)	@ sha1m q0,q3,q13
	vadd.i32	q13,q11,q7
	INST(0x8e,0x83,0xba,0xf3)	@ sha1su1 q4,q7
	INST(0x4e,0xac,0x3c,0xe2)	@ sha1su0 q5,q6,q7
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 14
	INST(0x68,0x0c,0x24,0xe2)	@ sha1m q0,q2,q12
	vadd.i32	q12,q11,q4
	INST(0x88,0xa3,0xba,0xf3)	@ sha1su1 q5,q4
	INST(0x48,0xcc,0x3e,0xe2)	@ sha1su0 q6,q7,q4
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 15
	INST(0x6a,0x0c,0x16,0xe2)	@ sha1p q0,q3,q13
	vadd.i32	q13,q11,q5
	INST(0x8a,0xc3,0xba,0xf3)	@ sha1su1 q6,q5
	INST(0x4a,0xec,0x38,0xe2)	@ sha1su0 q7,q4,q5
	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 16
	INST(0x68,0x0c,0x14,0xe2)	@ sha1p q0,q2,q12
	vadd.i32	q12,q11,q6
	INST(0x8c,0xe3,0xba,0xf3)	@ sha1su1 q7,q6
	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 17
	INST(0x6a,0x0c,0x16,0xe2)	@ sha1p q0,q3,q13
	vadd.i32	q13,q11,q7

	INST(0xc0,0x62,0xb9,0xf3)	@ sha1h q3,q0		@ 18
	INST(0x68,0x0c,0x14,0xe2)	@ sha1p q0,q2,q12

	INST(0xc0,0x42,0xb9,0xf3)	@ sha1h q2,q0		@ 19
	INST(0x6a,0x0c,0x16,0xe2)	@ sha1p q0,q3,q13

	vadd.i32	q1,q1,q2
	vadd.i32	q0,q0,q14
	bne	.Loop_v8

	vst1.32	{q0},[r0]!
	vst1.32	{d2[0]},[r0]

	vldmia	sp!,{d8,d9,d10,d11,d12,d13,d14,d15}
	bx	lr					@ bx lr
.size	sha1_block_data_order_armv8,.-sha1_block_data_order_armv8
#endif
#if __ARM_MAX_ARCH__>=7
.comm	OPENSSL_armcap_P,4,4
#endif
