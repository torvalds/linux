/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from keccak1600-armv4.pl. */
#include "arm_arch.h"

.text

#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif

.type	iotas32, %object
.align	5
iotas32:
.long	0x00000001, 0x00000000
.long	0x00000000, 0x00000089
.long	0x00000000, 0x8000008b
.long	0x00000000, 0x80008080
.long	0x00000001, 0x0000008b
.long	0x00000001, 0x00008000
.long	0x00000001, 0x80008088
.long	0x00000001, 0x80000082
.long	0x00000000, 0x0000000b
.long	0x00000000, 0x0000000a
.long	0x00000001, 0x00008082
.long	0x00000000, 0x00008003
.long	0x00000001, 0x0000808b
.long	0x00000001, 0x8000000b
.long	0x00000001, 0x8000008a
.long	0x00000001, 0x80000081
.long	0x00000000, 0x80000081
.long	0x00000000, 0x80000008
.long	0x00000000, 0x00000083
.long	0x00000000, 0x80008003
.long	0x00000001, 0x80008088
.long	0x00000000, 0x80000088
.long	0x00000001, 0x00008000
.long	0x00000000, 0x80008082
.size	iotas32,.-iotas32

.type	KeccakF1600_int, %function
.align	5
KeccakF1600_int:
	add	r9,sp,#176
	add	r12,sp,#0
	add	r10,sp,#40
	ldmia	r9,{r4,r5,r6,r7,r8,r9}		@ A[4][2..4]
KeccakF1600_enter:
	str	lr,[sp,#440]
	eor	r11,r11,r11
	str	r11,[sp,#444]
	b	.Lround2x

.align	4
.Lround2x:
	ldmia	r12,{r0,r1,r2,r3}		@ A[0][0..1]
	ldmia	r10,{r10,r11,r12,r14}	@ A[1][0..1]
#ifdef	__thumb2__
	eor	r0,r0,r10
	eor	r1,r1,r11
	eor	r2,r2,r12
	ldrd	r10,r11,[sp,#56]
	eor	r3,r3,r14
	ldrd	r12,r14,[sp,#64]
	eor	r4,r4,r10
	eor	r5,r5,r11
	eor	r6,r6,r12
	ldrd	r10,r11,[sp,#72]
	eor	r7,r7,r14
	ldrd	r12,r14,[sp,#80]
	eor	r8,r8,r10
	eor	r9,r9,r11
	eor	r0,r0,r12
	ldrd	r10,r11,[sp,#88]
	eor	r1,r1,r14
	ldrd	r12,r14,[sp,#96]
	eor	r2,r2,r10
	eor	r3,r3,r11
	eor	r4,r4,r12
	ldrd	r10,r11,[sp,#104]
	eor	r5,r5,r14
	ldrd	r12,r14,[sp,#112]
	eor	r6,r6,r10
	eor	r7,r7,r11
	eor	r8,r8,r12
	ldrd	r10,r11,[sp,#120]
	eor	r9,r9,r14
	ldrd	r12,r14,[sp,#128]
	eor	r0,r0,r10
	eor	r1,r1,r11
	eor	r2,r2,r12
	ldrd	r10,r11,[sp,#136]
	eor	r3,r3,r14
	ldrd	r12,r14,[sp,#144]
	eor	r4,r4,r10
	eor	r5,r5,r11
	eor	r6,r6,r12
	ldrd	r10,r11,[sp,#152]
	eor	r7,r7,r14
	ldrd	r12,r14,[sp,#160]
	eor	r8,r8,r10
	eor	r9,r9,r11
	eor	r0,r0,r12
	ldrd	r10,r11,[sp,#168]
	eor	r1,r1,r14
	ldrd	r12,r14,[sp,#16]
	eor	r2,r2,r10
	eor	r3,r3,r11
	eor	r4,r4,r12
	ldrd	r10,r11,[sp,#24]
	eor	r5,r5,r14
	ldrd	r12,r14,[sp,#32]
#else
	eor	r0,r0,r10
	add	r10,sp,#56
	eor	r1,r1,r11
	eor	r2,r2,r12
	eor	r3,r3,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[1][2..3]
	eor	r4,r4,r10
	add	r10,sp,#72
	eor	r5,r5,r11
	eor	r6,r6,r12
	eor	r7,r7,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[1][4]..A[2][0]
	eor	r8,r8,r10
	add	r10,sp,#88
	eor	r9,r9,r11
	eor	r0,r0,r12
	eor	r1,r1,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[2][1..2]
	eor	r2,r2,r10
	add	r10,sp,#104
	eor	r3,r3,r11
	eor	r4,r4,r12
	eor	r5,r5,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[2][3..4]
	eor	r6,r6,r10
	add	r10,sp,#120
	eor	r7,r7,r11
	eor	r8,r8,r12
	eor	r9,r9,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[3][0..1]
	eor	r0,r0,r10
	add	r10,sp,#136
	eor	r1,r1,r11
	eor	r2,r2,r12
	eor	r3,r3,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[3][2..3]
	eor	r4,r4,r10
	add	r10,sp,#152
	eor	r5,r5,r11
	eor	r6,r6,r12
	eor	r7,r7,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[3][4]..A[4][0]
	eor	r8,r8,r10
	ldr	r10,[sp,#168]		@ A[4][1]
	eor	r9,r9,r11
	ldr	r11,[sp,#168+4]
	eor	r0,r0,r12
	ldr	r12,[sp,#16]		@ A[0][2]
	eor	r1,r1,r14
	ldr	r14,[sp,#16+4]
	eor	r2,r2,r10
	add	r10,sp,#24
	eor	r3,r3,r11
	eor	r4,r4,r12
	eor	r5,r5,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[0][3..4]
#endif
	eor	r6,r6,r10
	eor	r7,r7,r11
	eor	r8,r8,r12
	eor	r9,r9,r14

	eor	r10,r0,r5,ror#32-1	@ E[0] = ROL64(C[2], 1) ^ C[0];
#ifndef	__thumb2__
	str	r10,[sp,#208]		@ D[1] = E[0]
#endif
	eor	r11,r1,r4
#ifndef	__thumb2__
	str	r11,[sp,#208+4]
#else
	strd	r10,r11,[sp,#208]		@ D[1] = E[0]
#endif
	eor	r12,r6,r1,ror#32-1	@ E[1] = ROL64(C[0], 1) ^ C[3];
	eor	r14,r7,r0
#ifndef	__thumb2__
	str	r12,[sp,#232]		@ D[4] = E[1]
#endif
	eor	r0,r8,r3,ror#32-1	@ C[0] = ROL64(C[1], 1) ^ C[4];
#ifndef	__thumb2__
	str	r14,[sp,#232+4]
#else
	strd	r12,r14,[sp,#232]		@ D[4] = E[1]
#endif
	eor	r1,r9,r2
#ifndef	__thumb2__
	str	r0,[sp,#200]		@ D[0] = C[0]
#endif
	eor	r2,r2,r7,ror#32-1	@ C[1] = ROL64(C[3], 1) ^ C[1];
#ifndef	__thumb2__
	ldr	r7,[sp,#144]
#endif
	eor	r3,r3,r6
#ifndef	__thumb2__
	str	r1,[sp,#200+4]
#else
	strd	r0,r1,[sp,#200]		@ D[0] = C[0]
#endif
#ifndef	__thumb2__
	ldr	r6,[sp,#144+4]
#else
	ldrd	r7,r6,[sp,#144]
#endif
#ifndef	__thumb2__
	str	r2,[sp,#216]		@ D[2] = C[1]
#endif
	eor	r4,r4,r9,ror#32-1	@ C[2] = ROL64(C[4], 1) ^ C[2];
#ifndef	__thumb2__
	str	r3,[sp,#216+4]
#else
	strd	r2,r3,[sp,#216]		@ D[2] = C[1]
#endif
	eor	r5,r5,r8

#ifndef	__thumb2__
	ldr	r8,[sp,#192]
#endif
#ifndef	__thumb2__
	ldr	r9,[sp,#192+4]
#else
	ldrd	r8,r9,[sp,#192]
#endif
#ifndef	__thumb2__
	str	r4,[sp,#224]		@ D[3] = C[2]
#endif
	eor	r7,r7,r4
#ifndef	__thumb2__
	str	r5,[sp,#224+4]
#else
	strd	r4,r5,[sp,#224]		@ D[3] = C[2]
#endif
	eor	r6,r6,r5
#ifndef	__thumb2__
	ldr	r4,[sp,#0]
#endif
	@ mov	r7,r7,ror#32-10		@ C[3] = ROL64(A[3][3] ^ C[2], rhotates[3][3]);   /* D[3] */
	@ mov	r6,r6,ror#32-11
#ifndef	__thumb2__
	ldr	r5,[sp,#0+4]
#else
	ldrd	r4,r5,[sp,#0]
#endif
	eor	r8,r8,r12
	eor	r9,r9,r14
#ifndef	__thumb2__
	ldr	r12,[sp,#96]
#endif
	eor	r0,r0,r4
#ifndef	__thumb2__
	ldr	r14,[sp,#96+4]
#else
	ldrd	r12,r14,[sp,#96]
#endif
	@ mov	r8,r8,ror#32-7		@ C[4] = ROL64(A[4][4] ^ E[1], rhotates[4][4]);   /* D[4] */
	@ mov	r9,r9,ror#32-7
	eor	r1,r1,r5		@ C[0] =       A[0][0] ^ C[0];
	eor	r12,r12,r2
#ifndef	__thumb2__
	ldr	r2,[sp,#48]
#endif
	eor	r14,r14,r3
#ifndef	__thumb2__
	ldr	r3,[sp,#48+4]
#else
	ldrd	r2,r3,[sp,#48]
#endif
	mov	r5,r12,ror#32-21		@ C[2] = ROL64(A[2][2] ^ C[1], rhotates[2][2]);
	ldr	r12,[sp,#444]			@ load counter
	eor	r2,r2,r10
	adr	r10,iotas32
	mov	r4,r14,ror#32-22
	add	r14,r10,r12
	eor	r3,r3,r11
	ldmia	r14,{r10,r11}		@ iotas[i]
	bic	r12,r4,r2,ror#32-22
	bic	r14,r5,r3,ror#32-22
	mov	r2,r2,ror#32-22		@ C[1] = ROL64(A[1][1] ^ E[0], rhotates[1][1]);
	mov	r3,r3,ror#32-22
	eor	r12,r12,r0
	eor	r14,r14,r1
	eor	r10,r10,r12
	eor	r11,r11,r14
#ifndef	__thumb2__
	str	r10,[sp,#240]		@ R[0][0] = C[0] ^ (~C[1] & C[2]) ^ iotas[i];
#endif
	bic	r12,r6,r4,ror#11
#ifndef	__thumb2__
	str	r11,[sp,#240+4]
#else
	strd	r10,r11,[sp,#240]		@ R[0][0] = C[0] ^ (~C[1] & C[2]) ^ iotas[i];
#endif
	bic	r14,r7,r5,ror#10
	bic	r10,r8,r6,ror#32-(11-7)
	bic	r11,r9,r7,ror#32-(10-7)
	eor	r12,r2,r12,ror#32-11
#ifndef	__thumb2__
	str	r12,[sp,#248]		@ R[0][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r14,r3,r14,ror#32-10
#ifndef	__thumb2__
	str	r14,[sp,#248+4]
#else
	strd	r12,r14,[sp,#248]		@ R[0][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r10,r4,r10,ror#32-7
	eor	r11,r5,r11,ror#32-7
#ifndef	__thumb2__
	str	r10,[sp,#256]		@ R[0][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r12,r0,r8,ror#32-7
#ifndef	__thumb2__
	str	r11,[sp,#256+4]
#else
	strd	r10,r11,[sp,#256]		@ R[0][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r14,r1,r9,ror#32-7
	eor	r12,r12,r6,ror#32-11
#ifndef	__thumb2__
	str	r12,[sp,#264]		@ R[0][3] = C[3] ^ (~C[4] & C[0]);
#endif
	eor	r14,r14,r7,ror#32-10
#ifndef	__thumb2__
	str	r14,[sp,#264+4]
#else
	strd	r12,r14,[sp,#264]		@ R[0][3] = C[3] ^ (~C[4] & C[0]);
#endif
	bic	r10,r2,r0
	add	r14,sp,#224
#ifndef	__thumb2__
	ldr	r0,[sp,#24]		@ A[0][3]
#endif
	bic	r11,r3,r1
#ifndef	__thumb2__
	ldr	r1,[sp,#24+4]
#else
	ldrd	r0,r1,[sp,#24]		@ A[0][3]
#endif
	eor	r10,r10,r8,ror#32-7
	eor	r11,r11,r9,ror#32-7
#ifndef	__thumb2__
	str	r10,[sp,#272]		@ R[0][4] = C[4] ^ (~C[0] & C[1]);
#endif
	add	r9,sp,#200
#ifndef	__thumb2__
	str	r11,[sp,#272+4]
#else
	strd	r10,r11,[sp,#272]		@ R[0][4] = C[4] ^ (~C[0] & C[1]);
#endif

	ldmia	r14,{r10,r11,r12,r14}	@ D[3..4]
	ldmia	r9,{r6,r7,r8,r9}		@ D[0..1]

#ifndef	__thumb2__
	ldr	r2,[sp,#72]		@ A[1][4]
#endif
	eor	r0,r0,r10
#ifndef	__thumb2__
	ldr	r3,[sp,#72+4]
#else
	ldrd	r2,r3,[sp,#72]		@ A[1][4]
#endif
	eor	r1,r1,r11
	@ mov	r0,r0,ror#32-14		@ C[0] = ROL64(A[0][3] ^ D[3], rhotates[0][3]);
#ifndef	__thumb2__
	ldr	r10,[sp,#128]		@ A[3][1]
#endif
	@ mov	r1,r1,ror#32-14
#ifndef	__thumb2__
	ldr	r11,[sp,#128+4]
#else
	ldrd	r10,r11,[sp,#128]		@ A[3][1]
#endif

	eor	r2,r2,r12
#ifndef	__thumb2__
	ldr	r4,[sp,#80]		@ A[2][0]
#endif
	eor	r3,r3,r14
#ifndef	__thumb2__
	ldr	r5,[sp,#80+4]
#else
	ldrd	r4,r5,[sp,#80]		@ A[2][0]
#endif
	@ mov	r2,r2,ror#32-10		@ C[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);
	@ mov	r3,r3,ror#32-10

	eor	r6,r6,r4
#ifndef	__thumb2__
	ldr	r12,[sp,#216]		@ D[2]
#endif
	eor	r7,r7,r5
#ifndef	__thumb2__
	ldr	r14,[sp,#216+4]
#else
	ldrd	r12,r14,[sp,#216]		@ D[2]
#endif
	mov	r5,r6,ror#32-1		@ C[2] = ROL64(A[2][0] ^ D[0], rhotates[2][0]);
	mov	r4,r7,ror#32-2

	eor	r10,r10,r8
#ifndef	__thumb2__
	ldr	r8,[sp,#176]		@ A[4][2]
#endif
	eor	r11,r11,r9
#ifndef	__thumb2__
	ldr	r9,[sp,#176+4]
#else
	ldrd	r8,r9,[sp,#176]		@ A[4][2]
#endif
	mov	r7,r10,ror#32-22		@ C[3] = ROL64(A[3][1] ^ D[1], rhotates[3][1]);
	mov	r6,r11,ror#32-23

	bic	r10,r4,r2,ror#32-10
	bic	r11,r5,r3,ror#32-10
	eor	r12,r12,r8
	eor	r14,r14,r9
	mov	r9,r12,ror#32-30		@ C[4] = ROL64(A[4][2] ^ D[2], rhotates[4][2]);
	mov	r8,r14,ror#32-31
	eor	r10,r10,r0,ror#32-14
	eor	r11,r11,r1,ror#32-14
#ifndef	__thumb2__
	str	r10,[sp,#280]		@ R[1][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r12,r6,r4
#ifndef	__thumb2__
	str	r11,[sp,#280+4]
#else
	strd	r10,r11,[sp,#280]		@ R[1][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r14,r7,r5
	eor	r12,r12,r2,ror#32-10
#ifndef	__thumb2__
	str	r12,[sp,#288]		@ R[1][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r14,r14,r3,ror#32-10
#ifndef	__thumb2__
	str	r14,[sp,#288+4]
#else
	strd	r12,r14,[sp,#288]		@ R[1][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r10,r8,r6
	bic	r11,r9,r7
	bic	r12,r0,r8,ror#14
	bic	r14,r1,r9,ror#14
	eor	r10,r10,r4
	eor	r11,r11,r5
#ifndef	__thumb2__
	str	r10,[sp,#296]		@ R[1][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r2,r2,r0,ror#32-(14-10)
#ifndef	__thumb2__
	str	r11,[sp,#296+4]
#else
	strd	r10,r11,[sp,#296]		@ R[1][2] = C[2] ^ (~C[3] & C[4]);
#endif
	eor	r12,r6,r12,ror#32-14
	bic	r11,r3,r1,ror#32-(14-10)
#ifndef	__thumb2__
	str	r12,[sp,#304]		@ R[1][3] = C[3] ^ (~C[4] & C[0]);
#endif
	eor	r14,r7,r14,ror#32-14
#ifndef	__thumb2__
	str	r14,[sp,#304+4]
#else
	strd	r12,r14,[sp,#304]		@ R[1][3] = C[3] ^ (~C[4] & C[0]);
#endif
	add	r12,sp,#208
#ifndef	__thumb2__
	ldr	r1,[sp,#8]		@ A[0][1]
#endif
	eor	r10,r8,r2,ror#32-10
#ifndef	__thumb2__
	ldr	r0,[sp,#8+4]
#else
	ldrd	r1,r0,[sp,#8]		@ A[0][1]
#endif
	eor	r11,r9,r11,ror#32-10
#ifndef	__thumb2__
	str	r10,[sp,#312]		@ R[1][4] = C[4] ^ (~C[0] & C[1]);
#endif
#ifndef	__thumb2__
	str	r11,[sp,#312+4]
#else
	strd	r10,r11,[sp,#312]		@ R[1][4] = C[4] ^ (~C[0] & C[1]);
#endif

	add	r9,sp,#224
	ldmia	r12,{r10,r11,r12,r14}	@ D[1..2]
#ifndef	__thumb2__
	ldr	r2,[sp,#56]		@ A[1][2]
#endif
#ifndef	__thumb2__
	ldr	r3,[sp,#56+4]
#else
	ldrd	r2,r3,[sp,#56]		@ A[1][2]
#endif
	ldmia	r9,{r6,r7,r8,r9}		@ D[3..4]

	eor	r1,r1,r10
#ifndef	__thumb2__
	ldr	r4,[sp,#104]		@ A[2][3]
#endif
	eor	r0,r0,r11
#ifndef	__thumb2__
	ldr	r5,[sp,#104+4]
#else
	ldrd	r4,r5,[sp,#104]		@ A[2][3]
#endif
	mov	r0,r0,ror#32-1		@ C[0] = ROL64(A[0][1] ^ D[1], rhotates[0][1]);

	eor	r2,r2,r12
#ifndef	__thumb2__
	ldr	r10,[sp,#152]		@ A[3][4]
#endif
	eor	r3,r3,r14
#ifndef	__thumb2__
	ldr	r11,[sp,#152+4]
#else
	ldrd	r10,r11,[sp,#152]		@ A[3][4]
#endif
	@ mov	r2,r2,ror#32-3		@ C[1] = ROL64(A[1][2] ^ D[2], rhotates[1][2]);
#ifndef	__thumb2__
	ldr	r12,[sp,#200]		@ D[0]
#endif
	@ mov	r3,r3,ror#32-3
#ifndef	__thumb2__
	ldr	r14,[sp,#200+4]
#else
	ldrd	r12,r14,[sp,#200]		@ D[0]
#endif

	eor	r4,r4,r6
	eor	r5,r5,r7
	@ mov	r5,r6,ror#32-12		@ C[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
	@ mov	r4,r7,ror#32-13		@ [track reverse order below]

	eor	r10,r10,r8
#ifndef	__thumb2__
	ldr	r8,[sp,#160]		@ A[4][0]
#endif
	eor	r11,r11,r9
#ifndef	__thumb2__
	ldr	r9,[sp,#160+4]
#else
	ldrd	r8,r9,[sp,#160]		@ A[4][0]
#endif
	mov	r6,r10,ror#32-4		@ C[3] = ROL64(A[3][4] ^ D[4], rhotates[3][4]);
	mov	r7,r11,ror#32-4

	eor	r12,r12,r8
	eor	r14,r14,r9
	mov	r8,r12,ror#32-9		@ C[4] = ROL64(A[4][0] ^ D[0], rhotates[4][0]);
	mov	r9,r14,ror#32-9

	bic	r10,r5,r2,ror#13-3
	bic	r11,r4,r3,ror#12-3
	bic	r12,r6,r5,ror#32-13
	bic	r14,r7,r4,ror#32-12
	eor	r10,r0,r10,ror#32-13
	eor	r11,r1,r11,ror#32-12
#ifndef	__thumb2__
	str	r10,[sp,#320]		@ R[2][0] = C[0] ^ (~C[1] & C[2])
#endif
	eor	r12,r12,r2,ror#32-3
#ifndef	__thumb2__
	str	r11,[sp,#320+4]
#else
	strd	r10,r11,[sp,#320]		@ R[2][0] = C[0] ^ (~C[1] & C[2])
#endif
	eor	r14,r14,r3,ror#32-3
#ifndef	__thumb2__
	str	r12,[sp,#328]		@ R[2][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r10,r8,r6
	bic	r11,r9,r7
#ifndef	__thumb2__
	str	r14,[sp,#328+4]
#else
	strd	r12,r14,[sp,#328]		@ R[2][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r10,r10,r5,ror#32-13
	eor	r11,r11,r4,ror#32-12
#ifndef	__thumb2__
	str	r10,[sp,#336]		@ R[2][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r12,r0,r8
#ifndef	__thumb2__
	str	r11,[sp,#336+4]
#else
	strd	r10,r11,[sp,#336]		@ R[2][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r14,r1,r9
	eor	r12,r12,r6
	eor	r14,r14,r7
#ifndef	__thumb2__
	str	r12,[sp,#344]		@ R[2][3] = C[3] ^ (~C[4] & C[0]);
#endif
	bic	r10,r2,r0,ror#3
#ifndef	__thumb2__
	str	r14,[sp,#344+4]
#else
	strd	r12,r14,[sp,#344]		@ R[2][3] = C[3] ^ (~C[4] & C[0]);
#endif
	bic	r11,r3,r1,ror#3
#ifndef	__thumb2__
	ldr	r1,[sp,#32]		@ A[0][4] [in reverse order]
#endif
	eor	r10,r8,r10,ror#32-3
#ifndef	__thumb2__
	ldr	r0,[sp,#32+4]
#else
	ldrd	r1,r0,[sp,#32]		@ A[0][4] [in reverse order]
#endif
	eor	r11,r9,r11,ror#32-3
#ifndef	__thumb2__
	str	r10,[sp,#352]		@ R[2][4] = C[4] ^ (~C[0] & C[1]);
#endif
	add	r9,sp,#208
#ifndef	__thumb2__
	str	r11,[sp,#352+4]
#else
	strd	r10,r11,[sp,#352]		@ R[2][4] = C[4] ^ (~C[0] & C[1]);
#endif

#ifndef	__thumb2__
	ldr	r10,[sp,#232]		@ D[4]
#endif
#ifndef	__thumb2__
	ldr	r11,[sp,#232+4]
#else
	ldrd	r10,r11,[sp,#232]		@ D[4]
#endif
#ifndef	__thumb2__
	ldr	r12,[sp,#200]		@ D[0]
#endif
#ifndef	__thumb2__
	ldr	r14,[sp,#200+4]
#else
	ldrd	r12,r14,[sp,#200]		@ D[0]
#endif

	ldmia	r9,{r6,r7,r8,r9}		@ D[1..2]

	eor	r1,r1,r10
#ifndef	__thumb2__
	ldr	r2,[sp,#40]		@ A[1][0]
#endif
	eor	r0,r0,r11
#ifndef	__thumb2__
	ldr	r3,[sp,#40+4]
#else
	ldrd	r2,r3,[sp,#40]		@ A[1][0]
#endif
	@ mov	r1,r10,ror#32-13		@ C[0] = ROL64(A[0][4] ^ D[4], rhotates[0][4]);
#ifndef	__thumb2__
	ldr	r4,[sp,#88]		@ A[2][1]
#endif
	@ mov	r0,r11,ror#32-14		@ [was loaded in reverse order]
#ifndef	__thumb2__
	ldr	r5,[sp,#88+4]
#else
	ldrd	r4,r5,[sp,#88]		@ A[2][1]
#endif

	eor	r2,r2,r12
#ifndef	__thumb2__
	ldr	r10,[sp,#136]		@ A[3][2]
#endif
	eor	r3,r3,r14
#ifndef	__thumb2__
	ldr	r11,[sp,#136+4]
#else
	ldrd	r10,r11,[sp,#136]		@ A[3][2]
#endif
	@ mov	r2,r2,ror#32-18		@ C[1] = ROL64(A[1][0] ^ D[0], rhotates[1][0]);
#ifndef	__thumb2__
	ldr	r12,[sp,#224]		@ D[3]
#endif
	@ mov	r3,r3,ror#32-18
#ifndef	__thumb2__
	ldr	r14,[sp,#224+4]
#else
	ldrd	r12,r14,[sp,#224]		@ D[3]
#endif

	eor	r6,r6,r4
	eor	r7,r7,r5
	mov	r4,r6,ror#32-5		@ C[2] = ROL64(A[2][1] ^ D[1], rhotates[2][1]);
	mov	r5,r7,ror#32-5

	eor	r10,r10,r8
#ifndef	__thumb2__
	ldr	r8,[sp,#184]		@ A[4][3]
#endif
	eor	r11,r11,r9
#ifndef	__thumb2__
	ldr	r9,[sp,#184+4]
#else
	ldrd	r8,r9,[sp,#184]		@ A[4][3]
#endif
	mov	r7,r10,ror#32-7		@ C[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
	mov	r6,r11,ror#32-8

	eor	r12,r12,r8
	eor	r14,r14,r9
	mov	r8,r12,ror#32-28		@ C[4] = ROL64(A[4][3] ^ D[3], rhotates[4][3]);
	mov	r9,r14,ror#32-28

	bic	r10,r4,r2,ror#32-18
	bic	r11,r5,r3,ror#32-18
	eor	r10,r10,r0,ror#32-14
	eor	r11,r11,r1,ror#32-13
#ifndef	__thumb2__
	str	r10,[sp,#360]		@ R[3][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r12,r6,r4
#ifndef	__thumb2__
	str	r11,[sp,#360+4]
#else
	strd	r10,r11,[sp,#360]		@ R[3][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r14,r7,r5
	eor	r12,r12,r2,ror#32-18
#ifndef	__thumb2__
	str	r12,[sp,#368]		@ R[3][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r14,r14,r3,ror#32-18
#ifndef	__thumb2__
	str	r14,[sp,#368+4]
#else
	strd	r12,r14,[sp,#368]		@ R[3][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r10,r8,r6
	bic	r11,r9,r7
	bic	r12,r0,r8,ror#14
	bic	r14,r1,r9,ror#13
	eor	r10,r10,r4
	eor	r11,r11,r5
#ifndef	__thumb2__
	str	r10,[sp,#376]		@ R[3][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r2,r2,r0,ror#18-14
#ifndef	__thumb2__
	str	r11,[sp,#376+4]
#else
	strd	r10,r11,[sp,#376]		@ R[3][2] = C[2] ^ (~C[3] & C[4]);
#endif
	eor	r12,r6,r12,ror#32-14
	bic	r11,r3,r1,ror#18-13
	eor	r14,r7,r14,ror#32-13
#ifndef	__thumb2__
	str	r12,[sp,#384]		@ R[3][3] = C[3] ^ (~C[4] & C[0]);
#endif
#ifndef	__thumb2__
	str	r14,[sp,#384+4]
#else
	strd	r12,r14,[sp,#384]		@ R[3][3] = C[3] ^ (~C[4] & C[0]);
#endif
	add	r14,sp,#216
#ifndef	__thumb2__
	ldr	r0,[sp,#16]		@ A[0][2]
#endif
	eor	r10,r8,r2,ror#32-18
#ifndef	__thumb2__
	ldr	r1,[sp,#16+4]
#else
	ldrd	r0,r1,[sp,#16]		@ A[0][2]
#endif
	eor	r11,r9,r11,ror#32-18
#ifndef	__thumb2__
	str	r10,[sp,#392]		@ R[3][4] = C[4] ^ (~C[0] & C[1]);
#endif
#ifndef	__thumb2__
	str	r11,[sp,#392+4]
#else
	strd	r10,r11,[sp,#392]		@ R[3][4] = C[4] ^ (~C[0] & C[1]);
#endif

	ldmia	r14,{r10,r11,r12,r14}	@ D[2..3]
#ifndef	__thumb2__
	ldr	r2,[sp,#64]		@ A[1][3]
#endif
#ifndef	__thumb2__
	ldr	r3,[sp,#64+4]
#else
	ldrd	r2,r3,[sp,#64]		@ A[1][3]
#endif
#ifndef	__thumb2__
	ldr	r6,[sp,#232]		@ D[4]
#endif
#ifndef	__thumb2__
	ldr	r7,[sp,#232+4]
#else
	ldrd	r6,r7,[sp,#232]		@ D[4]
#endif

	eor	r0,r0,r10
#ifndef	__thumb2__
	ldr	r4,[sp,#112]		@ A[2][4]
#endif
	eor	r1,r1,r11
#ifndef	__thumb2__
	ldr	r5,[sp,#112+4]
#else
	ldrd	r4,r5,[sp,#112]		@ A[2][4]
#endif
	@ mov	r0,r0,ror#32-31		@ C[0] = ROL64(A[0][2] ^ D[2], rhotates[0][2]);
#ifndef	__thumb2__
	ldr	r8,[sp,#200]		@ D[0]
#endif
	@ mov	r1,r1,ror#32-31
#ifndef	__thumb2__
	ldr	r9,[sp,#200+4]
#else
	ldrd	r8,r9,[sp,#200]		@ D[0]
#endif

	eor	r12,r12,r2
#ifndef	__thumb2__
	ldr	r10,[sp,#120]		@ A[3][0]
#endif
	eor	r14,r14,r3
#ifndef	__thumb2__
	ldr	r11,[sp,#120+4]
#else
	ldrd	r10,r11,[sp,#120]		@ A[3][0]
#endif
	mov	r3,r12,ror#32-27		@ C[1] = ROL64(A[1][3] ^ D[3], rhotates[1][3]);
#ifndef	__thumb2__
	ldr	r12,[sp,#208]		@ D[1]
#endif
	mov	r2,r14,ror#32-28
#ifndef	__thumb2__
	ldr	r14,[sp,#208+4]
#else
	ldrd	r12,r14,[sp,#208]		@ D[1]
#endif

	eor	r6,r6,r4
	eor	r7,r7,r5
	mov	r5,r6,ror#32-19		@ C[2] = ROL64(A[2][4] ^ D[4], rhotates[2][4]);
	mov	r4,r7,ror#32-20

	eor	r10,r10,r8
#ifndef	__thumb2__
	ldr	r8,[sp,#168]		@ A[4][1]
#endif
	eor	r11,r11,r9
#ifndef	__thumb2__
	ldr	r9,[sp,#168+4]
#else
	ldrd	r8,r9,[sp,#168]		@ A[4][1]
#endif
	mov	r7,r10,ror#32-20		@ C[3] = ROL64(A[3][0] ^ D[0], rhotates[3][0]);
	mov	r6,r11,ror#32-21

	eor	r8,r8,r12
	eor	r9,r9,r14
	@ mov	r8,r2,ror#32-1		@ C[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);
	@ mov	r9,r3,ror#32-1

	bic	r10,r4,r2
	bic	r11,r5,r3
	eor	r10,r10,r0,ror#32-31
#ifndef	__thumb2__
	str	r10,[sp,#400]		@ R[4][0] = C[0] ^ (~C[1] & C[2])
#endif
	eor	r11,r11,r1,ror#32-31
#ifndef	__thumb2__
	str	r11,[sp,#400+4]
#else
	strd	r10,r11,[sp,#400]		@ R[4][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r12,r6,r4
	bic	r14,r7,r5
	eor	r12,r12,r2
	eor	r14,r14,r3
#ifndef	__thumb2__
	str	r12,[sp,#408]		@ R[4][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r10,r8,r6,ror#1
#ifndef	__thumb2__
	str	r14,[sp,#408+4]
#else
	strd	r12,r14,[sp,#408]		@ R[4][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r11,r9,r7,ror#1
	bic	r12,r0,r8,ror#31-1
	bic	r14,r1,r9,ror#31-1
	eor	r4,r4,r10,ror#32-1
#ifndef	__thumb2__
	str	r4,[sp,#416]		@ R[4][2] = C[2] ^= (~C[3] & C[4]);
#endif
	eor	r5,r5,r11,ror#32-1
#ifndef	__thumb2__
	str	r5,[sp,#416+4]
#else
	strd	r4,r5,[sp,#416]		@ R[4][2] = C[2] ^= (~C[3] & C[4]);
#endif
	eor	r6,r6,r12,ror#32-31
	eor	r7,r7,r14,ror#32-31
#ifndef	__thumb2__
	str	r6,[sp,#424]		@ R[4][3] = C[3] ^= (~C[4] & C[0]);
#endif
	bic	r10,r2,r0,ror#32-31
#ifndef	__thumb2__
	str	r7,[sp,#424+4]
#else
	strd	r6,r7,[sp,#424]		@ R[4][3] = C[3] ^= (~C[4] & C[0]);
#endif
	bic	r11,r3,r1,ror#32-31
	add	r12,sp,#240
	eor	r8,r10,r8,ror#32-1
	add	r10,sp,#280
	eor	r9,r11,r9,ror#32-1
#ifndef	__thumb2__
	str	r8,[sp,#432]		@ R[4][4] = C[4] ^= (~C[0] & C[1]);
#endif
#ifndef	__thumb2__
	str	r9,[sp,#432+4]
#else
	strd	r8,r9,[sp,#432]		@ R[4][4] = C[4] ^= (~C[0] & C[1]);
#endif
	ldmia	r12,{r0,r1,r2,r3}		@ A[0][0..1]
	ldmia	r10,{r10,r11,r12,r14}	@ A[1][0..1]
#ifdef	__thumb2__
	eor	r0,r0,r10
	eor	r1,r1,r11
	eor	r2,r2,r12
	ldrd	r10,r11,[sp,#296]
	eor	r3,r3,r14
	ldrd	r12,r14,[sp,#304]
	eor	r4,r4,r10
	eor	r5,r5,r11
	eor	r6,r6,r12
	ldrd	r10,r11,[sp,#312]
	eor	r7,r7,r14
	ldrd	r12,r14,[sp,#320]
	eor	r8,r8,r10
	eor	r9,r9,r11
	eor	r0,r0,r12
	ldrd	r10,r11,[sp,#328]
	eor	r1,r1,r14
	ldrd	r12,r14,[sp,#336]
	eor	r2,r2,r10
	eor	r3,r3,r11
	eor	r4,r4,r12
	ldrd	r10,r11,[sp,#344]
	eor	r5,r5,r14
	ldrd	r12,r14,[sp,#352]
	eor	r6,r6,r10
	eor	r7,r7,r11
	eor	r8,r8,r12
	ldrd	r10,r11,[sp,#360]
	eor	r9,r9,r14
	ldrd	r12,r14,[sp,#368]
	eor	r0,r0,r10
	eor	r1,r1,r11
	eor	r2,r2,r12
	ldrd	r10,r11,[sp,#376]
	eor	r3,r3,r14
	ldrd	r12,r14,[sp,#384]
	eor	r4,r4,r10
	eor	r5,r5,r11
	eor	r6,r6,r12
	ldrd	r10,r11,[sp,#392]
	eor	r7,r7,r14
	ldrd	r12,r14,[sp,#400]
	eor	r8,r8,r10
	eor	r9,r9,r11
	eor	r0,r0,r12
	ldrd	r10,r11,[sp,#408]
	eor	r1,r1,r14
	ldrd	r12,r14,[sp,#256]
	eor	r2,r2,r10
	eor	r3,r3,r11
	eor	r4,r4,r12
	ldrd	r10,r11,[sp,#264]
	eor	r5,r5,r14
	ldrd	r12,r14,[sp,#272]
#else
	eor	r0,r0,r10
	add	r10,sp,#296
	eor	r1,r1,r11
	eor	r2,r2,r12
	eor	r3,r3,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[1][2..3]
	eor	r4,r4,r10
	add	r10,sp,#312
	eor	r5,r5,r11
	eor	r6,r6,r12
	eor	r7,r7,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[1][4]..A[2][0]
	eor	r8,r8,r10
	add	r10,sp,#328
	eor	r9,r9,r11
	eor	r0,r0,r12
	eor	r1,r1,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[2][1..2]
	eor	r2,r2,r10
	add	r10,sp,#344
	eor	r3,r3,r11
	eor	r4,r4,r12
	eor	r5,r5,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[2][3..4]
	eor	r6,r6,r10
	add	r10,sp,#360
	eor	r7,r7,r11
	eor	r8,r8,r12
	eor	r9,r9,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[3][0..1]
	eor	r0,r0,r10
	add	r10,sp,#376
	eor	r1,r1,r11
	eor	r2,r2,r12
	eor	r3,r3,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[3][2..3]
	eor	r4,r4,r10
	add	r10,sp,#392
	eor	r5,r5,r11
	eor	r6,r6,r12
	eor	r7,r7,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[3][4]..A[4][0]
	eor	r8,r8,r10
	ldr	r10,[sp,#408]		@ A[4][1]
	eor	r9,r9,r11
	ldr	r11,[sp,#408+4]
	eor	r0,r0,r12
	ldr	r12,[sp,#256]		@ A[0][2]
	eor	r1,r1,r14
	ldr	r14,[sp,#256+4]
	eor	r2,r2,r10
	add	r10,sp,#264
	eor	r3,r3,r11
	eor	r4,r4,r12
	eor	r5,r5,r14
	ldmia	r10,{r10,r11,r12,r14}	@ A[0][3..4]
#endif
	eor	r6,r6,r10
	eor	r7,r7,r11
	eor	r8,r8,r12
	eor	r9,r9,r14

	eor	r10,r0,r5,ror#32-1	@ E[0] = ROL64(C[2], 1) ^ C[0];
#ifndef	__thumb2__
	str	r10,[sp,#208]		@ D[1] = E[0]
#endif
	eor	r11,r1,r4
#ifndef	__thumb2__
	str	r11,[sp,#208+4]
#else
	strd	r10,r11,[sp,#208]		@ D[1] = E[0]
#endif
	eor	r12,r6,r1,ror#32-1	@ E[1] = ROL64(C[0], 1) ^ C[3];
	eor	r14,r7,r0
#ifndef	__thumb2__
	str	r12,[sp,#232]		@ D[4] = E[1]
#endif
	eor	r0,r8,r3,ror#32-1	@ C[0] = ROL64(C[1], 1) ^ C[4];
#ifndef	__thumb2__
	str	r14,[sp,#232+4]
#else
	strd	r12,r14,[sp,#232]		@ D[4] = E[1]
#endif
	eor	r1,r9,r2
#ifndef	__thumb2__
	str	r0,[sp,#200]		@ D[0] = C[0]
#endif
	eor	r2,r2,r7,ror#32-1	@ C[1] = ROL64(C[3], 1) ^ C[1];
#ifndef	__thumb2__
	ldr	r7,[sp,#384]
#endif
	eor	r3,r3,r6
#ifndef	__thumb2__
	str	r1,[sp,#200+4]
#else
	strd	r0,r1,[sp,#200]		@ D[0] = C[0]
#endif
#ifndef	__thumb2__
	ldr	r6,[sp,#384+4]
#else
	ldrd	r7,r6,[sp,#384]
#endif
#ifndef	__thumb2__
	str	r2,[sp,#216]		@ D[2] = C[1]
#endif
	eor	r4,r4,r9,ror#32-1	@ C[2] = ROL64(C[4], 1) ^ C[2];
#ifndef	__thumb2__
	str	r3,[sp,#216+4]
#else
	strd	r2,r3,[sp,#216]		@ D[2] = C[1]
#endif
	eor	r5,r5,r8

#ifndef	__thumb2__
	ldr	r8,[sp,#432]
#endif
#ifndef	__thumb2__
	ldr	r9,[sp,#432+4]
#else
	ldrd	r8,r9,[sp,#432]
#endif
#ifndef	__thumb2__
	str	r4,[sp,#224]		@ D[3] = C[2]
#endif
	eor	r7,r7,r4
#ifndef	__thumb2__
	str	r5,[sp,#224+4]
#else
	strd	r4,r5,[sp,#224]		@ D[3] = C[2]
#endif
	eor	r6,r6,r5
#ifndef	__thumb2__
	ldr	r4,[sp,#240]
#endif
	@ mov	r7,r7,ror#32-10		@ C[3] = ROL64(A[3][3] ^ C[2], rhotates[3][3]);   /* D[3] */
	@ mov	r6,r6,ror#32-11
#ifndef	__thumb2__
	ldr	r5,[sp,#240+4]
#else
	ldrd	r4,r5,[sp,#240]
#endif
	eor	r8,r8,r12
	eor	r9,r9,r14
#ifndef	__thumb2__
	ldr	r12,[sp,#336]
#endif
	eor	r0,r0,r4
#ifndef	__thumb2__
	ldr	r14,[sp,#336+4]
#else
	ldrd	r12,r14,[sp,#336]
#endif
	@ mov	r8,r8,ror#32-7		@ C[4] = ROL64(A[4][4] ^ E[1], rhotates[4][4]);   /* D[4] */
	@ mov	r9,r9,ror#32-7
	eor	r1,r1,r5		@ C[0] =       A[0][0] ^ C[0];
	eor	r12,r12,r2
#ifndef	__thumb2__
	ldr	r2,[sp,#288]
#endif
	eor	r14,r14,r3
#ifndef	__thumb2__
	ldr	r3,[sp,#288+4]
#else
	ldrd	r2,r3,[sp,#288]
#endif
	mov	r5,r12,ror#32-21		@ C[2] = ROL64(A[2][2] ^ C[1], rhotates[2][2]);
	ldr	r12,[sp,#444]			@ load counter
	eor	r2,r2,r10
	adr	r10,iotas32
	mov	r4,r14,ror#32-22
	add	r14,r10,r12
	eor	r3,r3,r11
#ifndef	__thumb2__
	ldr	r10,[r14,#8]		@ iotas[i].lo
#endif
	add	r12,r12,#16
#ifndef	__thumb2__
	ldr	r11,[r14,#12]		@ iotas[i].hi
#else
	ldrd	r10,r11,[r14,#8]		@ iotas[i].lo
#endif
	cmp	r12,#192
	str	r12,[sp,#444]			@ store counter
	bic	r12,r4,r2,ror#32-22
	bic	r14,r5,r3,ror#32-22
	mov	r2,r2,ror#32-22		@ C[1] = ROL64(A[1][1] ^ E[0], rhotates[1][1]);
	mov	r3,r3,ror#32-22
	eor	r12,r12,r0
	eor	r14,r14,r1
	eor	r10,r10,r12
	eor	r11,r11,r14
#ifndef	__thumb2__
	str	r10,[sp,#0]		@ R[0][0] = C[0] ^ (~C[1] & C[2]) ^ iotas[i];
#endif
	bic	r12,r6,r4,ror#11
#ifndef	__thumb2__
	str	r11,[sp,#0+4]
#else
	strd	r10,r11,[sp,#0]		@ R[0][0] = C[0] ^ (~C[1] & C[2]) ^ iotas[i];
#endif
	bic	r14,r7,r5,ror#10
	bic	r10,r8,r6,ror#32-(11-7)
	bic	r11,r9,r7,ror#32-(10-7)
	eor	r12,r2,r12,ror#32-11
#ifndef	__thumb2__
	str	r12,[sp,#8]		@ R[0][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r14,r3,r14,ror#32-10
#ifndef	__thumb2__
	str	r14,[sp,#8+4]
#else
	strd	r12,r14,[sp,#8]		@ R[0][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r10,r4,r10,ror#32-7
	eor	r11,r5,r11,ror#32-7
#ifndef	__thumb2__
	str	r10,[sp,#16]		@ R[0][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r12,r0,r8,ror#32-7
#ifndef	__thumb2__
	str	r11,[sp,#16+4]
#else
	strd	r10,r11,[sp,#16]		@ R[0][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r14,r1,r9,ror#32-7
	eor	r12,r12,r6,ror#32-11
#ifndef	__thumb2__
	str	r12,[sp,#24]		@ R[0][3] = C[3] ^ (~C[4] & C[0]);
#endif
	eor	r14,r14,r7,ror#32-10
#ifndef	__thumb2__
	str	r14,[sp,#24+4]
#else
	strd	r12,r14,[sp,#24]		@ R[0][3] = C[3] ^ (~C[4] & C[0]);
#endif
	bic	r10,r2,r0
	add	r14,sp,#224
#ifndef	__thumb2__
	ldr	r0,[sp,#264]		@ A[0][3]
#endif
	bic	r11,r3,r1
#ifndef	__thumb2__
	ldr	r1,[sp,#264+4]
#else
	ldrd	r0,r1,[sp,#264]		@ A[0][3]
#endif
	eor	r10,r10,r8,ror#32-7
	eor	r11,r11,r9,ror#32-7
#ifndef	__thumb2__
	str	r10,[sp,#32]		@ R[0][4] = C[4] ^ (~C[0] & C[1]);
#endif
	add	r9,sp,#200
#ifndef	__thumb2__
	str	r11,[sp,#32+4]
#else
	strd	r10,r11,[sp,#32]		@ R[0][4] = C[4] ^ (~C[0] & C[1]);
#endif

	ldmia	r14,{r10,r11,r12,r14}	@ D[3..4]
	ldmia	r9,{r6,r7,r8,r9}		@ D[0..1]

#ifndef	__thumb2__
	ldr	r2,[sp,#312]		@ A[1][4]
#endif
	eor	r0,r0,r10
#ifndef	__thumb2__
	ldr	r3,[sp,#312+4]
#else
	ldrd	r2,r3,[sp,#312]		@ A[1][4]
#endif
	eor	r1,r1,r11
	@ mov	r0,r0,ror#32-14		@ C[0] = ROL64(A[0][3] ^ D[3], rhotates[0][3]);
#ifndef	__thumb2__
	ldr	r10,[sp,#368]		@ A[3][1]
#endif
	@ mov	r1,r1,ror#32-14
#ifndef	__thumb2__
	ldr	r11,[sp,#368+4]
#else
	ldrd	r10,r11,[sp,#368]		@ A[3][1]
#endif

	eor	r2,r2,r12
#ifndef	__thumb2__
	ldr	r4,[sp,#320]		@ A[2][0]
#endif
	eor	r3,r3,r14
#ifndef	__thumb2__
	ldr	r5,[sp,#320+4]
#else
	ldrd	r4,r5,[sp,#320]		@ A[2][0]
#endif
	@ mov	r2,r2,ror#32-10		@ C[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);
	@ mov	r3,r3,ror#32-10

	eor	r6,r6,r4
#ifndef	__thumb2__
	ldr	r12,[sp,#216]		@ D[2]
#endif
	eor	r7,r7,r5
#ifndef	__thumb2__
	ldr	r14,[sp,#216+4]
#else
	ldrd	r12,r14,[sp,#216]		@ D[2]
#endif
	mov	r5,r6,ror#32-1		@ C[2] = ROL64(A[2][0] ^ D[0], rhotates[2][0]);
	mov	r4,r7,ror#32-2

	eor	r10,r10,r8
#ifndef	__thumb2__
	ldr	r8,[sp,#416]		@ A[4][2]
#endif
	eor	r11,r11,r9
#ifndef	__thumb2__
	ldr	r9,[sp,#416+4]
#else
	ldrd	r8,r9,[sp,#416]		@ A[4][2]
#endif
	mov	r7,r10,ror#32-22		@ C[3] = ROL64(A[3][1] ^ D[1], rhotates[3][1]);
	mov	r6,r11,ror#32-23

	bic	r10,r4,r2,ror#32-10
	bic	r11,r5,r3,ror#32-10
	eor	r12,r12,r8
	eor	r14,r14,r9
	mov	r9,r12,ror#32-30		@ C[4] = ROL64(A[4][2] ^ D[2], rhotates[4][2]);
	mov	r8,r14,ror#32-31
	eor	r10,r10,r0,ror#32-14
	eor	r11,r11,r1,ror#32-14
#ifndef	__thumb2__
	str	r10,[sp,#40]		@ R[1][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r12,r6,r4
#ifndef	__thumb2__
	str	r11,[sp,#40+4]
#else
	strd	r10,r11,[sp,#40]		@ R[1][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r14,r7,r5
	eor	r12,r12,r2,ror#32-10
#ifndef	__thumb2__
	str	r12,[sp,#48]		@ R[1][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r14,r14,r3,ror#32-10
#ifndef	__thumb2__
	str	r14,[sp,#48+4]
#else
	strd	r12,r14,[sp,#48]		@ R[1][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r10,r8,r6
	bic	r11,r9,r7
	bic	r12,r0,r8,ror#14
	bic	r14,r1,r9,ror#14
	eor	r10,r10,r4
	eor	r11,r11,r5
#ifndef	__thumb2__
	str	r10,[sp,#56]		@ R[1][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r2,r2,r0,ror#32-(14-10)
#ifndef	__thumb2__
	str	r11,[sp,#56+4]
#else
	strd	r10,r11,[sp,#56]		@ R[1][2] = C[2] ^ (~C[3] & C[4]);
#endif
	eor	r12,r6,r12,ror#32-14
	bic	r11,r3,r1,ror#32-(14-10)
#ifndef	__thumb2__
	str	r12,[sp,#64]		@ R[1][3] = C[3] ^ (~C[4] & C[0]);
#endif
	eor	r14,r7,r14,ror#32-14
#ifndef	__thumb2__
	str	r14,[sp,#64+4]
#else
	strd	r12,r14,[sp,#64]		@ R[1][3] = C[3] ^ (~C[4] & C[0]);
#endif
	add	r12,sp,#208
#ifndef	__thumb2__
	ldr	r1,[sp,#248]		@ A[0][1]
#endif
	eor	r10,r8,r2,ror#32-10
#ifndef	__thumb2__
	ldr	r0,[sp,#248+4]
#else
	ldrd	r1,r0,[sp,#248]		@ A[0][1]
#endif
	eor	r11,r9,r11,ror#32-10
#ifndef	__thumb2__
	str	r10,[sp,#72]		@ R[1][4] = C[4] ^ (~C[0] & C[1]);
#endif
#ifndef	__thumb2__
	str	r11,[sp,#72+4]
#else
	strd	r10,r11,[sp,#72]		@ R[1][4] = C[4] ^ (~C[0] & C[1]);
#endif

	add	r9,sp,#224
	ldmia	r12,{r10,r11,r12,r14}	@ D[1..2]
#ifndef	__thumb2__
	ldr	r2,[sp,#296]		@ A[1][2]
#endif
#ifndef	__thumb2__
	ldr	r3,[sp,#296+4]
#else
	ldrd	r2,r3,[sp,#296]		@ A[1][2]
#endif
	ldmia	r9,{r6,r7,r8,r9}		@ D[3..4]

	eor	r1,r1,r10
#ifndef	__thumb2__
	ldr	r4,[sp,#344]		@ A[2][3]
#endif
	eor	r0,r0,r11
#ifndef	__thumb2__
	ldr	r5,[sp,#344+4]
#else
	ldrd	r4,r5,[sp,#344]		@ A[2][3]
#endif
	mov	r0,r0,ror#32-1		@ C[0] = ROL64(A[0][1] ^ D[1], rhotates[0][1]);

	eor	r2,r2,r12
#ifndef	__thumb2__
	ldr	r10,[sp,#392]		@ A[3][4]
#endif
	eor	r3,r3,r14
#ifndef	__thumb2__
	ldr	r11,[sp,#392+4]
#else
	ldrd	r10,r11,[sp,#392]		@ A[3][4]
#endif
	@ mov	r2,r2,ror#32-3		@ C[1] = ROL64(A[1][2] ^ D[2], rhotates[1][2]);
#ifndef	__thumb2__
	ldr	r12,[sp,#200]		@ D[0]
#endif
	@ mov	r3,r3,ror#32-3
#ifndef	__thumb2__
	ldr	r14,[sp,#200+4]
#else
	ldrd	r12,r14,[sp,#200]		@ D[0]
#endif

	eor	r4,r4,r6
	eor	r5,r5,r7
	@ mov	r5,r6,ror#32-12		@ C[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
	@ mov	r4,r7,ror#32-13		@ [track reverse order below]

	eor	r10,r10,r8
#ifndef	__thumb2__
	ldr	r8,[sp,#400]		@ A[4][0]
#endif
	eor	r11,r11,r9
#ifndef	__thumb2__
	ldr	r9,[sp,#400+4]
#else
	ldrd	r8,r9,[sp,#400]		@ A[4][0]
#endif
	mov	r6,r10,ror#32-4		@ C[3] = ROL64(A[3][4] ^ D[4], rhotates[3][4]);
	mov	r7,r11,ror#32-4

	eor	r12,r12,r8
	eor	r14,r14,r9
	mov	r8,r12,ror#32-9		@ C[4] = ROL64(A[4][0] ^ D[0], rhotates[4][0]);
	mov	r9,r14,ror#32-9

	bic	r10,r5,r2,ror#13-3
	bic	r11,r4,r3,ror#12-3
	bic	r12,r6,r5,ror#32-13
	bic	r14,r7,r4,ror#32-12
	eor	r10,r0,r10,ror#32-13
	eor	r11,r1,r11,ror#32-12
#ifndef	__thumb2__
	str	r10,[sp,#80]		@ R[2][0] = C[0] ^ (~C[1] & C[2])
#endif
	eor	r12,r12,r2,ror#32-3
#ifndef	__thumb2__
	str	r11,[sp,#80+4]
#else
	strd	r10,r11,[sp,#80]		@ R[2][0] = C[0] ^ (~C[1] & C[2])
#endif
	eor	r14,r14,r3,ror#32-3
#ifndef	__thumb2__
	str	r12,[sp,#88]		@ R[2][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r10,r8,r6
	bic	r11,r9,r7
#ifndef	__thumb2__
	str	r14,[sp,#88+4]
#else
	strd	r12,r14,[sp,#88]		@ R[2][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r10,r10,r5,ror#32-13
	eor	r11,r11,r4,ror#32-12
#ifndef	__thumb2__
	str	r10,[sp,#96]		@ R[2][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r12,r0,r8
#ifndef	__thumb2__
	str	r11,[sp,#96+4]
#else
	strd	r10,r11,[sp,#96]		@ R[2][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r14,r1,r9
	eor	r12,r12,r6
	eor	r14,r14,r7
#ifndef	__thumb2__
	str	r12,[sp,#104]		@ R[2][3] = C[3] ^ (~C[4] & C[0]);
#endif
	bic	r10,r2,r0,ror#3
#ifndef	__thumb2__
	str	r14,[sp,#104+4]
#else
	strd	r12,r14,[sp,#104]		@ R[2][3] = C[3] ^ (~C[4] & C[0]);
#endif
	bic	r11,r3,r1,ror#3
#ifndef	__thumb2__
	ldr	r1,[sp,#272]		@ A[0][4] [in reverse order]
#endif
	eor	r10,r8,r10,ror#32-3
#ifndef	__thumb2__
	ldr	r0,[sp,#272+4]
#else
	ldrd	r1,r0,[sp,#272]		@ A[0][4] [in reverse order]
#endif
	eor	r11,r9,r11,ror#32-3
#ifndef	__thumb2__
	str	r10,[sp,#112]		@ R[2][4] = C[4] ^ (~C[0] & C[1]);
#endif
	add	r9,sp,#208
#ifndef	__thumb2__
	str	r11,[sp,#112+4]
#else
	strd	r10,r11,[sp,#112]		@ R[2][4] = C[4] ^ (~C[0] & C[1]);
#endif

#ifndef	__thumb2__
	ldr	r10,[sp,#232]		@ D[4]
#endif
#ifndef	__thumb2__
	ldr	r11,[sp,#232+4]
#else
	ldrd	r10,r11,[sp,#232]		@ D[4]
#endif
#ifndef	__thumb2__
	ldr	r12,[sp,#200]		@ D[0]
#endif
#ifndef	__thumb2__
	ldr	r14,[sp,#200+4]
#else
	ldrd	r12,r14,[sp,#200]		@ D[0]
#endif

	ldmia	r9,{r6,r7,r8,r9}		@ D[1..2]

	eor	r1,r1,r10
#ifndef	__thumb2__
	ldr	r2,[sp,#280]		@ A[1][0]
#endif
	eor	r0,r0,r11
#ifndef	__thumb2__
	ldr	r3,[sp,#280+4]
#else
	ldrd	r2,r3,[sp,#280]		@ A[1][0]
#endif
	@ mov	r1,r10,ror#32-13		@ C[0] = ROL64(A[0][4] ^ D[4], rhotates[0][4]);
#ifndef	__thumb2__
	ldr	r4,[sp,#328]		@ A[2][1]
#endif
	@ mov	r0,r11,ror#32-14		@ [was loaded in reverse order]
#ifndef	__thumb2__
	ldr	r5,[sp,#328+4]
#else
	ldrd	r4,r5,[sp,#328]		@ A[2][1]
#endif

	eor	r2,r2,r12
#ifndef	__thumb2__
	ldr	r10,[sp,#376]		@ A[3][2]
#endif
	eor	r3,r3,r14
#ifndef	__thumb2__
	ldr	r11,[sp,#376+4]
#else
	ldrd	r10,r11,[sp,#376]		@ A[3][2]
#endif
	@ mov	r2,r2,ror#32-18		@ C[1] = ROL64(A[1][0] ^ D[0], rhotates[1][0]);
#ifndef	__thumb2__
	ldr	r12,[sp,#224]		@ D[3]
#endif
	@ mov	r3,r3,ror#32-18
#ifndef	__thumb2__
	ldr	r14,[sp,#224+4]
#else
	ldrd	r12,r14,[sp,#224]		@ D[3]
#endif

	eor	r6,r6,r4
	eor	r7,r7,r5
	mov	r4,r6,ror#32-5		@ C[2] = ROL64(A[2][1] ^ D[1], rhotates[2][1]);
	mov	r5,r7,ror#32-5

	eor	r10,r10,r8
#ifndef	__thumb2__
	ldr	r8,[sp,#424]		@ A[4][3]
#endif
	eor	r11,r11,r9
#ifndef	__thumb2__
	ldr	r9,[sp,#424+4]
#else
	ldrd	r8,r9,[sp,#424]		@ A[4][3]
#endif
	mov	r7,r10,ror#32-7		@ C[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
	mov	r6,r11,ror#32-8

	eor	r12,r12,r8
	eor	r14,r14,r9
	mov	r8,r12,ror#32-28		@ C[4] = ROL64(A[4][3] ^ D[3], rhotates[4][3]);
	mov	r9,r14,ror#32-28

	bic	r10,r4,r2,ror#32-18
	bic	r11,r5,r3,ror#32-18
	eor	r10,r10,r0,ror#32-14
	eor	r11,r11,r1,ror#32-13
#ifndef	__thumb2__
	str	r10,[sp,#120]		@ R[3][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r12,r6,r4
#ifndef	__thumb2__
	str	r11,[sp,#120+4]
#else
	strd	r10,r11,[sp,#120]		@ R[3][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r14,r7,r5
	eor	r12,r12,r2,ror#32-18
#ifndef	__thumb2__
	str	r12,[sp,#128]		@ R[3][1] = C[1] ^ (~C[2] & C[3]);
#endif
	eor	r14,r14,r3,ror#32-18
#ifndef	__thumb2__
	str	r14,[sp,#128+4]
#else
	strd	r12,r14,[sp,#128]		@ R[3][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r10,r8,r6
	bic	r11,r9,r7
	bic	r12,r0,r8,ror#14
	bic	r14,r1,r9,ror#13
	eor	r10,r10,r4
	eor	r11,r11,r5
#ifndef	__thumb2__
	str	r10,[sp,#136]		@ R[3][2] = C[2] ^ (~C[3] & C[4]);
#endif
	bic	r2,r2,r0,ror#18-14
#ifndef	__thumb2__
	str	r11,[sp,#136+4]
#else
	strd	r10,r11,[sp,#136]		@ R[3][2] = C[2] ^ (~C[3] & C[4]);
#endif
	eor	r12,r6,r12,ror#32-14
	bic	r11,r3,r1,ror#18-13
	eor	r14,r7,r14,ror#32-13
#ifndef	__thumb2__
	str	r12,[sp,#144]		@ R[3][3] = C[3] ^ (~C[4] & C[0]);
#endif
#ifndef	__thumb2__
	str	r14,[sp,#144+4]
#else
	strd	r12,r14,[sp,#144]		@ R[3][3] = C[3] ^ (~C[4] & C[0]);
#endif
	add	r14,sp,#216
#ifndef	__thumb2__
	ldr	r0,[sp,#256]		@ A[0][2]
#endif
	eor	r10,r8,r2,ror#32-18
#ifndef	__thumb2__
	ldr	r1,[sp,#256+4]
#else
	ldrd	r0,r1,[sp,#256]		@ A[0][2]
#endif
	eor	r11,r9,r11,ror#32-18
#ifndef	__thumb2__
	str	r10,[sp,#152]		@ R[3][4] = C[4] ^ (~C[0] & C[1]);
#endif
#ifndef	__thumb2__
	str	r11,[sp,#152+4]
#else
	strd	r10,r11,[sp,#152]		@ R[3][4] = C[4] ^ (~C[0] & C[1]);
#endif

	ldmia	r14,{r10,r11,r12,r14}	@ D[2..3]
#ifndef	__thumb2__
	ldr	r2,[sp,#304]		@ A[1][3]
#endif
#ifndef	__thumb2__
	ldr	r3,[sp,#304+4]
#else
	ldrd	r2,r3,[sp,#304]		@ A[1][3]
#endif
#ifndef	__thumb2__
	ldr	r6,[sp,#232]		@ D[4]
#endif
#ifndef	__thumb2__
	ldr	r7,[sp,#232+4]
#else
	ldrd	r6,r7,[sp,#232]		@ D[4]
#endif

	eor	r0,r0,r10
#ifndef	__thumb2__
	ldr	r4,[sp,#352]		@ A[2][4]
#endif
	eor	r1,r1,r11
#ifndef	__thumb2__
	ldr	r5,[sp,#352+4]
#else
	ldrd	r4,r5,[sp,#352]		@ A[2][4]
#endif
	@ mov	r0,r0,ror#32-31		@ C[0] = ROL64(A[0][2] ^ D[2], rhotates[0][2]);
#ifndef	__thumb2__
	ldr	r8,[sp,#200]		@ D[0]
#endif
	@ mov	r1,r1,ror#32-31
#ifndef	__thumb2__
	ldr	r9,[sp,#200+4]
#else
	ldrd	r8,r9,[sp,#200]		@ D[0]
#endif

	eor	r12,r12,r2
#ifndef	__thumb2__
	ldr	r10,[sp,#360]		@ A[3][0]
#endif
	eor	r14,r14,r3
#ifndef	__thumb2__
	ldr	r11,[sp,#360+4]
#else
	ldrd	r10,r11,[sp,#360]		@ A[3][0]
#endif
	mov	r3,r12,ror#32-27		@ C[1] = ROL64(A[1][3] ^ D[3], rhotates[1][3]);
#ifndef	__thumb2__
	ldr	r12,[sp,#208]		@ D[1]
#endif
	mov	r2,r14,ror#32-28
#ifndef	__thumb2__
	ldr	r14,[sp,#208+4]
#else
	ldrd	r12,r14,[sp,#208]		@ D[1]
#endif

	eor	r6,r6,r4
	eor	r7,r7,r5
	mov	r5,r6,ror#32-19		@ C[2] = ROL64(A[2][4] ^ D[4], rhotates[2][4]);
	mov	r4,r7,ror#32-20

	eor	r10,r10,r8
#ifndef	__thumb2__
	ldr	r8,[sp,#408]		@ A[4][1]
#endif
	eor	r11,r11,r9
#ifndef	__thumb2__
	ldr	r9,[sp,#408+4]
#else
	ldrd	r8,r9,[sp,#408]		@ A[4][1]
#endif
	mov	r7,r10,ror#32-20		@ C[3] = ROL64(A[3][0] ^ D[0], rhotates[3][0]);
	mov	r6,r11,ror#32-21

	eor	r8,r8,r12
	eor	r9,r9,r14
	@ mov	r8,r2,ror#32-1		@ C[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);
	@ mov	r9,r3,ror#32-1

	bic	r10,r4,r2
	bic	r11,r5,r3
	eor	r10,r10,r0,ror#32-31
#ifndef	__thumb2__
	str	r10,[sp,#160]		@ R[4][0] = C[0] ^ (~C[1] & C[2])
#endif
	eor	r11,r11,r1,ror#32-31
#ifndef	__thumb2__
	str	r11,[sp,#160+4]
#else
	strd	r10,r11,[sp,#160]		@ R[4][0] = C[0] ^ (~C[1] & C[2])
#endif
	bic	r12,r6,r4
	bic	r14,r7,r5
	eor	r12,r12,r2
	eor	r14,r14,r3
#ifndef	__thumb2__
	str	r12,[sp,#168]		@ R[4][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r10,r8,r6,ror#1
#ifndef	__thumb2__
	str	r14,[sp,#168+4]
#else
	strd	r12,r14,[sp,#168]		@ R[4][1] = C[1] ^ (~C[2] & C[3]);
#endif
	bic	r11,r9,r7,ror#1
	bic	r12,r0,r8,ror#31-1
	bic	r14,r1,r9,ror#31-1
	eor	r4,r4,r10,ror#32-1
#ifndef	__thumb2__
	str	r4,[sp,#176]		@ R[4][2] = C[2] ^= (~C[3] & C[4]);
#endif
	eor	r5,r5,r11,ror#32-1
#ifndef	__thumb2__
	str	r5,[sp,#176+4]
#else
	strd	r4,r5,[sp,#176]		@ R[4][2] = C[2] ^= (~C[3] & C[4]);
#endif
	eor	r6,r6,r12,ror#32-31
	eor	r7,r7,r14,ror#32-31
#ifndef	__thumb2__
	str	r6,[sp,#184]		@ R[4][3] = C[3] ^= (~C[4] & C[0]);
#endif
	bic	r10,r2,r0,ror#32-31
#ifndef	__thumb2__
	str	r7,[sp,#184+4]
#else
	strd	r6,r7,[sp,#184]		@ R[4][3] = C[3] ^= (~C[4] & C[0]);
#endif
	bic	r11,r3,r1,ror#32-31
	add	r12,sp,#0
	eor	r8,r10,r8,ror#32-1
	add	r10,sp,#40
	eor	r9,r11,r9,ror#32-1
#ifndef	__thumb2__
	str	r8,[sp,#192]		@ R[4][4] = C[4] ^= (~C[0] & C[1]);
#endif
#ifndef	__thumb2__
	str	r9,[sp,#192+4]
#else
	strd	r8,r9,[sp,#192]		@ R[4][4] = C[4] ^= (~C[0] & C[1]);
#endif
	blo	.Lround2x

	ldr	pc,[sp,#440]
.size	KeccakF1600_int,.-KeccakF1600_int

.type	KeccakF1600, %function
.align	5
KeccakF1600:
	stmdb	sp!,{r0,r4-r11,lr}
	sub	sp,sp,#440+16			@ space for A[5][5],D[5],T[5][5],...

	add	r10,r0,#40
	add	r11,sp,#40
	ldmia	r0,    {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}		@ copy A[5][5] to stack
	stmia	sp,    {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10, {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	add	r12,sp,#0
	add	r10,sp,#40
	stmia	r11, {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}

	bl	KeccakF1600_enter

	ldr	r11, [sp,#440+16]		@ restore pointer to A
	ldmia	sp,    {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}		@ return A[5][5]
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10, {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11, {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}

	add	sp,sp,#440+20
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,pc}
.size	KeccakF1600,.-KeccakF1600
.globl	SHA3_absorb
.type	SHA3_absorb,%function
.align	5
SHA3_absorb:
	stmdb	sp!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
	sub	sp,sp,#456+16

	add	r10,r0,#40
	@ mov	r11,r1
	mov	r12,r2
	mov	r14,r3
	cmp	r2,r3
	blo	.Labsorb_abort

	add	r11,sp,#0
	ldmia	r0,      {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}	@ copy A[5][5] to stack
	stmia	r11!,   {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,   {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,   {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11!,   {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r11,    {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}

	ldr	r11,[sp,#476]		@ restore r11
#ifdef	__thumb2__
	mov	r9,#0x00ff00ff
	mov	r8,#0x0f0f0f0f
	mov	r7,#0x33333333
	mov	r6,#0x55555555
#else
	mov	r6,#0x11		@ compose constants
	mov	r8,#0x0f
	mov	r9,#0xff
	orr	r6,r6,r6,lsl#8
	orr	r8,r8,r8,lsl#8
	orr	r6,r6,r6,lsl#16		@ 0x11111111
	orr	r9,r9,r9,lsl#16		@ 0x00ff00ff
	orr	r8,r8,r8,lsl#16		@ 0x0f0f0f0f
	orr	r7,r6,r6,lsl#1		@ 0x33333333
	orr	r6,r6,r6,lsl#2		@ 0x55555555
#endif
	str	r9,[sp,#468]
	str	r8,[sp,#464]
	str	r7,[sp,#460]
	str	r6,[sp,#456]
	b	.Loop_absorb

.align	4
.Loop_absorb:
	subs	r0,r12,r14
	blo	.Labsorbed
	add	r10,sp,#0
	str	r0,[sp,#480]		@ save len - bsz

.align	4
.Loop_block:
	ldrb	r0,[r11],#1
	ldrb	r1,[r11],#1
	ldrb	r2,[r11],#1
	ldrb	r3,[r11],#1
	ldrb	r4,[r11],#1
	orr	r0,r0,r1,lsl#8
	ldrb	r1,[r11],#1
	orr	r0,r0,r2,lsl#16
	ldrb	r2,[r11],#1
	orr	r0,r0,r3,lsl#24		@ lo
	ldrb	r3,[r11],#1
	orr	r1,r4,r1,lsl#8
	orr	r1,r1,r2,lsl#16
	orr	r1,r1,r3,lsl#24		@ hi

	and	r2,r0,r6		@ &=0x55555555
	and	r0,r0,r6,lsl#1		@ &=0xaaaaaaaa
	and	r3,r1,r6		@ &=0x55555555
	and	r1,r1,r6,lsl#1		@ &=0xaaaaaaaa
	orr	r2,r2,r2,lsr#1
	orr	r0,r0,r0,lsl#1
	orr	r3,r3,r3,lsr#1
	orr	r1,r1,r1,lsl#1
	and	r2,r2,r7		@ &=0x33333333
	and	r0,r0,r7,lsl#2		@ &=0xcccccccc
	and	r3,r3,r7		@ &=0x33333333
	and	r1,r1,r7,lsl#2		@ &=0xcccccccc
	orr	r2,r2,r2,lsr#2
	orr	r0,r0,r0,lsl#2
	orr	r3,r3,r3,lsr#2
	orr	r1,r1,r1,lsl#2
	and	r2,r2,r8		@ &=0x0f0f0f0f
	and	r0,r0,r8,lsl#4		@ &=0xf0f0f0f0
	and	r3,r3,r8		@ &=0x0f0f0f0f
	and	r1,r1,r8,lsl#4		@ &=0xf0f0f0f0
	ldmia	r10,{r4,r5}		@ A_flat[i]
	orr	r2,r2,r2,lsr#4
	orr	r0,r0,r0,lsl#4
	orr	r3,r3,r3,lsr#4
	orr	r1,r1,r1,lsl#4
	and	r2,r2,r9		@ &=0x00ff00ff
	and	r0,r0,r9,lsl#8		@ &=0xff00ff00
	and	r3,r3,r9		@ &=0x00ff00ff
	and	r1,r1,r9,lsl#8		@ &=0xff00ff00
	orr	r2,r2,r2,lsr#8
	orr	r0,r0,r0,lsl#8
	orr	r3,r3,r3,lsr#8
	orr	r1,r1,r1,lsl#8

	mov	r2,r2,lsl#16
	mov	r1,r1,lsr#16
	eor	r4,r4,r3,lsl#16
	eor	r5,r5,r0,lsr#16
	eor	r4,r4,r2,lsr#16
	eor	r5,r5,r1,lsl#16
	stmia	r10!,{r4,r5}	@ A_flat[i++] ^= BitInterleave(inp[0..7])

	subs	r14,r14,#8
	bhi	.Loop_block

	str	r11,[sp,#476]

	bl	KeccakF1600_int

	add	r14,sp,#456
	ldmia	r14,{r6,r7,r8,r9,r10,r11,r12,r14}	@ restore constants and variables
	b	.Loop_absorb

.align	4
.Labsorbed:
	add	r11,sp,#40
	ldmia	sp,      {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}	@ return A[5][5]
	ldmia	r11!,   {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r11!,   {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r11!,   {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r10!,{r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	ldmia	r11,    {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}
	stmia	r10, {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9}

.Labsorb_abort:
	add	sp,sp,#456+32
	mov	r0,r12			@ return value
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,r11,r12,pc}
.size	SHA3_absorb,.-SHA3_absorb
.globl	SHA3_squeeze
.type	SHA3_squeeze,%function
.align	5
SHA3_squeeze:
	stmdb	sp!,{r0,r3-r10,lr}

	mov	r10,r0
	mov	r4,r1
	mov	r5,r2
	mov	r12,r3

#ifdef	__thumb2__
	mov	r9,#0x00ff00ff
	mov	r8,#0x0f0f0f0f
	mov	r7,#0x33333333
	mov	r6,#0x55555555
#else
	mov	r6,#0x11		@ compose constants
	mov	r8,#0x0f
	mov	r9,#0xff
	orr	r6,r6,r6,lsl#8
	orr	r8,r8,r8,lsl#8
	orr	r6,r6,r6,lsl#16		@ 0x11111111
	orr	r9,r9,r9,lsl#16		@ 0x00ff00ff
	orr	r8,r8,r8,lsl#16		@ 0x0f0f0f0f
	orr	r7,r6,r6,lsl#1		@ 0x33333333
	orr	r6,r6,r6,lsl#2		@ 0x55555555
#endif
	stmdb	sp!,{r6,r7,r8,r9}

	mov	r14,r10
	b	.Loop_squeeze

.align	4
.Loop_squeeze:
	ldmia	r10!,{r0,r1}	@ A_flat[i++]

	mov	r2,r0,lsl#16
	mov	r3,r1,lsl#16		@ r3 = r1 << 16
	mov	r2,r2,lsr#16		@ r2 = r0 & 0x0000ffff
	mov	r1,r1,lsr#16
	mov	r0,r0,lsr#16		@ r0 = r0 >> 16
	mov	r1,r1,lsl#16		@ r1 = r1 & 0xffff0000

	orr	r2,r2,r2,lsl#8
	orr	r3,r3,r3,lsr#8
	orr	r0,r0,r0,lsl#8
	orr	r1,r1,r1,lsr#8
	and	r2,r2,r9		@ &=0x00ff00ff
	and	r3,r3,r9,lsl#8		@ &=0xff00ff00
	and	r0,r0,r9		@ &=0x00ff00ff
	and	r1,r1,r9,lsl#8		@ &=0xff00ff00
	orr	r2,r2,r2,lsl#4
	orr	r3,r3,r3,lsr#4
	orr	r0,r0,r0,lsl#4
	orr	r1,r1,r1,lsr#4
	and	r2,r2,r8		@ &=0x0f0f0f0f
	and	r3,r3,r8,lsl#4		@ &=0xf0f0f0f0
	and	r0,r0,r8		@ &=0x0f0f0f0f
	and	r1,r1,r8,lsl#4		@ &=0xf0f0f0f0
	orr	r2,r2,r2,lsl#2
	orr	r3,r3,r3,lsr#2
	orr	r0,r0,r0,lsl#2
	orr	r1,r1,r1,lsr#2
	and	r2,r2,r7		@ &=0x33333333
	and	r3,r3,r7,lsl#2		@ &=0xcccccccc
	and	r0,r0,r7		@ &=0x33333333
	and	r1,r1,r7,lsl#2		@ &=0xcccccccc
	orr	r2,r2,r2,lsl#1
	orr	r3,r3,r3,lsr#1
	orr	r0,r0,r0,lsl#1
	orr	r1,r1,r1,lsr#1
	and	r2,r2,r6		@ &=0x55555555
	and	r3,r3,r6,lsl#1		@ &=0xaaaaaaaa
	and	r0,r0,r6		@ &=0x55555555
	and	r1,r1,r6,lsl#1		@ &=0xaaaaaaaa

	orr	r2,r2,r3
	orr	r0,r0,r1

	cmp	r5,#8
	blo	.Lsqueeze_tail
	mov	r1,r2,lsr#8
	strb	r2,[r4],#1
	mov	r3,r2,lsr#16
	strb	r1,[r4],#1
	mov	r2,r2,lsr#24
	strb	r3,[r4],#1
	strb	r2,[r4],#1

	mov	r1,r0,lsr#8
	strb	r0,[r4],#1
	mov	r3,r0,lsr#16
	strb	r1,[r4],#1
	mov	r0,r0,lsr#24
	strb	r3,[r4],#1
	strb	r0,[r4],#1
	subs	r5,r5,#8
	beq	.Lsqueeze_done

	subs	r12,r12,#8		@ bsz -= 8
	bhi	.Loop_squeeze

	mov	r0,r14			@ original r10

	bl	KeccakF1600

	ldmia	sp,{r6,r7,r8,r9,r10,r12}		@ restore constants and variables
	mov	r14,r10
	b	.Loop_squeeze

.align	4
.Lsqueeze_tail:
	strb	r2,[r4],#1
	mov	r2,r2,lsr#8
	subs	r5,r5,#1
	beq	.Lsqueeze_done
	strb	r2,[r4],#1
	mov	r2,r2,lsr#8
	subs	r5,r5,#1
	beq	.Lsqueeze_done
	strb	r2,[r4],#1
	mov	r2,r2,lsr#8
	subs	r5,r5,#1
	beq	.Lsqueeze_done
	strb	r2,[r4],#1
	subs	r5,r5,#1
	beq	.Lsqueeze_done

	strb	r0,[r4],#1
	mov	r0,r0,lsr#8
	subs	r5,r5,#1
	beq	.Lsqueeze_done
	strb	r0,[r4],#1
	mov	r0,r0,lsr#8
	subs	r5,r5,#1
	beq	.Lsqueeze_done
	strb	r0,[r4]
	b	.Lsqueeze_done

.align	4
.Lsqueeze_done:
	add	sp,sp,#24
	ldmia	sp!,{r4,r5,r6,r7,r8,r9,r10,pc}
.size	SHA3_squeeze,.-SHA3_squeeze
#if __ARM_MAX_ARCH__>=7
.fpu	neon

.type	iotas64, %object
.align	5
iotas64:
.quad	0x0000000000000001
.quad	0x0000000000008082
.quad	0x800000000000808a
.quad	0x8000000080008000
.quad	0x000000000000808b
.quad	0x0000000080000001
.quad	0x8000000080008081
.quad	0x8000000000008009
.quad	0x000000000000008a
.quad	0x0000000000000088
.quad	0x0000000080008009
.quad	0x000000008000000a
.quad	0x000000008000808b
.quad	0x800000000000008b
.quad	0x8000000000008089
.quad	0x8000000000008003
.quad	0x8000000000008002
.quad	0x8000000000000080
.quad	0x000000000000800a
.quad	0x800000008000000a
.quad	0x8000000080008081
.quad	0x8000000000008080
.quad	0x0000000080000001
.quad	0x8000000080008008
.size	iotas64,.-iotas64

.type	KeccakF1600_neon, %function
.align	5
KeccakF1600_neon:
	add	r1, r0, #16
	adr	r2, iotas64
	mov	r3, #24			@ loop counter
	b	.Loop_neon

.align	4
.Loop_neon:
	@ Theta
	vst1.64	{q4},  [r0:64]		@ offload A[0..1][4]
	veor	q13, q0,  q5		@ A[0..1][0]^A[2..3][0]
	vst1.64	{d18}, [r1:64]		@ offload A[2][4]
	veor	q14, q1,  q6		@ A[0..1][1]^A[2..3][1]
	veor	q15, q2,  q7		@ A[0..1][2]^A[2..3][2]
	veor	d26, d26, d27		@ C[0]=A[0][0]^A[1][0]^A[2][0]^A[3][0]
	veor	d27, d28, d29		@ C[1]=A[0][1]^A[1][1]^A[2][1]^A[3][1]
	veor	q14, q3,  q8		@ A[0..1][3]^A[2..3][3]
	veor	q4,  q4,  q9		@ A[0..1][4]^A[2..3][4]
	veor	d30, d30, d31		@ C[2]=A[0][2]^A[1][2]^A[2][2]^A[3][2]
	veor	d31, d28, d29		@ C[3]=A[0][3]^A[1][3]^A[2][3]^A[3][3]
	veor	d25, d8,  d9		@ C[4]=A[0][4]^A[1][4]^A[2][4]^A[3][4]
	veor	q13, q13, q10		@ C[0..1]^=A[4][0..1]
	veor	q14, q15, q11		@ C[2..3]^=A[4][2..3]
	veor	d25, d25, d24		@ C[4]^=A[4][4]

	vadd.u64	q4,  q13, q13		@ C[0..1]<<1
	vadd.u64	q15, q14, q14		@ C[2..3]<<1
	vadd.u64	d18, d25, d25		@ C[4]<<1
	vsri.u64	q4,  q13, #63		@ ROL64(C[0..1],1)
	vsri.u64	q15, q14, #63		@ ROL64(C[2..3],1)
	vsri.u64	d18, d25, #63		@ ROL64(C[4],1)
	veor	d25, d25, d9		@ D[0] = C[4] ^= ROL64(C[1],1)
	veor	q13, q13, q15		@ D[1..2] = C[0..1] ^ ROL64(C[2..3],1)
	veor	d28, d28, d18		@ D[3] = C[2] ^= ROL64(C[4],1)
	veor	d29, d29, d8		@ D[4] = C[3] ^= ROL64(C[0],1)

	veor	d0,  d0,  d25		@ A[0][0] ^= C[4]
	veor	d1,  d1,  d25		@ A[1][0] ^= C[4]
	veor	d10, d10, d25		@ A[2][0] ^= C[4]
	veor	d11, d11, d25		@ A[3][0] ^= C[4]
	veor	d20, d20, d25		@ A[4][0] ^= C[4]

	veor	d2,  d2,  d26		@ A[0][1] ^= D[1]
	veor	d3,  d3,  d26		@ A[1][1] ^= D[1]
	veor	d12, d12, d26		@ A[2][1] ^= D[1]
	veor	d13, d13, d26		@ A[3][1] ^= D[1]
	veor	d21, d21, d26		@ A[4][1] ^= D[1]
	vmov	d26, d27

	veor	d6,  d6,  d28		@ A[0][3] ^= C[2]
	veor	d7,  d7,  d28		@ A[1][3] ^= C[2]
	veor	d16, d16, d28		@ A[2][3] ^= C[2]
	veor	d17, d17, d28		@ A[3][3] ^= C[2]
	veor	d23, d23, d28		@ A[4][3] ^= C[2]
	vld1.64	{q4},  [r0:64]		@ restore A[0..1][4]
	vmov	d28, d29

	vld1.64	{d18}, [r1:64]		@ restore A[2][4]
	veor	q2,  q2,  q13		@ A[0..1][2] ^= D[2]
	veor	q7,  q7,  q13		@ A[2..3][2] ^= D[2]
	veor	d22, d22, d27		@ A[4][2]    ^= D[2]

	veor	q4,  q4,  q14		@ A[0..1][4] ^= C[3]
	veor	q9,  q9,  q14		@ A[2..3][4] ^= C[3]
	veor	d24, d24, d29		@ A[4][4]    ^= C[3]

	@ Rho + Pi
	vmov	d26, d2			@ C[1] = A[0][1]
	vshl.u64	d2,  d3,  #44
	vmov	d27, d4			@ C[2] = A[0][2]
	vshl.u64	d4,  d14, #43
	vmov	d28, d6			@ C[3] = A[0][3]
	vshl.u64	d6,  d17, #21
	vmov	d29, d8			@ C[4] = A[0][4]
	vshl.u64	d8,  d24, #14
	vsri.u64	d2,  d3,  #64-44	@ A[0][1] = ROL64(A[1][1], rhotates[1][1])
	vsri.u64	d4,  d14, #64-43	@ A[0][2] = ROL64(A[2][2], rhotates[2][2])
	vsri.u64	d6,  d17, #64-21	@ A[0][3] = ROL64(A[3][3], rhotates[3][3])
	vsri.u64	d8,  d24, #64-14	@ A[0][4] = ROL64(A[4][4], rhotates[4][4])

	vshl.u64	d3,  d9,  #20
	vshl.u64	d14, d16, #25
	vshl.u64	d17, d15, #15
	vshl.u64	d24, d21, #2
	vsri.u64	d3,  d9,  #64-20	@ A[1][1] = ROL64(A[1][4], rhotates[1][4])
	vsri.u64	d14, d16, #64-25	@ A[2][2] = ROL64(A[2][3], rhotates[2][3])
	vsri.u64	d17, d15, #64-15	@ A[3][3] = ROL64(A[3][2], rhotates[3][2])
	vsri.u64	d24, d21, #64-2		@ A[4][4] = ROL64(A[4][1], rhotates[4][1])

	vshl.u64	d9,  d22, #61
	@ vshl.u64	d16, d19, #8
	vshl.u64	d15, d12, #10
	vshl.u64	d21, d7,  #55
	vsri.u64	d9,  d22, #64-61	@ A[1][4] = ROL64(A[4][2], rhotates[4][2])
	vext.8	d16, d19, d19, #8-1	@ A[2][3] = ROL64(A[3][4], rhotates[3][4])
	vsri.u64	d15, d12, #64-10	@ A[3][2] = ROL64(A[2][1], rhotates[2][1])
	vsri.u64	d21, d7,  #64-55	@ A[4][1] = ROL64(A[1][3], rhotates[1][3])

	vshl.u64	d22, d18, #39
	@ vshl.u64	d19, d23, #56
	vshl.u64	d12, d5,  #6
	vshl.u64	d7,  d13, #45
	vsri.u64	d22, d18, #64-39	@ A[4][2] = ROL64(A[2][4], rhotates[2][4])
	vext.8	d19, d23, d23, #8-7	@ A[3][4] = ROL64(A[4][3], rhotates[4][3])
	vsri.u64	d12, d5,  #64-6		@ A[2][1] = ROL64(A[1][2], rhotates[1][2])
	vsri.u64	d7,  d13, #64-45	@ A[1][3] = ROL64(A[3][1], rhotates[3][1])

	vshl.u64	d18, d20, #18
	vshl.u64	d23, d11, #41
	vshl.u64	d5,  d10, #3
	vshl.u64	d13, d1,  #36
	vsri.u64	d18, d20, #64-18	@ A[2][4] = ROL64(A[4][0], rhotates[4][0])
	vsri.u64	d23, d11, #64-41	@ A[4][3] = ROL64(A[3][0], rhotates[3][0])
	vsri.u64	d5,  d10, #64-3		@ A[1][2] = ROL64(A[2][0], rhotates[2][0])
	vsri.u64	d13, d1,  #64-36	@ A[3][1] = ROL64(A[1][0], rhotates[1][0])

	vshl.u64	d1,  d28, #28
	vshl.u64	d10, d26, #1
	vshl.u64	d11, d29, #27
	vshl.u64	d20, d27, #62
	vsri.u64	d1,  d28, #64-28	@ A[1][0] = ROL64(C[3],    rhotates[0][3])
	vsri.u64	d10, d26, #64-1		@ A[2][0] = ROL64(C[1],    rhotates[0][1])
	vsri.u64	d11, d29, #64-27	@ A[3][0] = ROL64(C[4],    rhotates[0][4])
	vsri.u64	d20, d27, #64-62	@ A[4][0] = ROL64(C[2],    rhotates[0][2])

	@ Chi + Iota
	vbic	q13, q2,  q1
	vbic	q14, q3,  q2
	vbic	q15, q4,  q3
	veor	q13, q13, q0		@ A[0..1][0] ^ (~A[0..1][1] & A[0..1][2])
	veor	q14, q14, q1		@ A[0..1][1] ^ (~A[0..1][2] & A[0..1][3])
	veor	q2,  q2,  q15		@ A[0..1][2] ^= (~A[0..1][3] & A[0..1][4])
	vst1.64	{q13}, [r0:64]		@ offload A[0..1][0]
	vbic	q13, q0,  q4
	vbic	q15, q1,  q0
	vmov	q1,  q14		@ A[0..1][1]
	veor	q3,  q3,  q13		@ A[0..1][3] ^= (~A[0..1][4] & A[0..1][0])
	veor	q4,  q4,  q15		@ A[0..1][4] ^= (~A[0..1][0] & A[0..1][1])

	vbic	q13, q7,  q6
	vmov	q0,  q5			@ A[2..3][0]
	vbic	q14, q8,  q7
	vmov	q15, q6			@ A[2..3][1]
	veor	q5,  q5,  q13		@ A[2..3][0] ^= (~A[2..3][1] & A[2..3][2])
	vbic	q13, q9,  q8
	veor	q6,  q6,  q14		@ A[2..3][1] ^= (~A[2..3][2] & A[2..3][3])
	vbic	q14, q0,  q9
	veor	q7,  q7,  q13		@ A[2..3][2] ^= (~A[2..3][3] & A[2..3][4])
	vbic	q13, q15, q0
	veor	q8,  q8,  q14		@ A[2..3][3] ^= (~A[2..3][4] & A[2..3][0])
	vmov	q14, q10		@ A[4][0..1]
	veor	q9,  q9,  q13		@ A[2..3][4] ^= (~A[2..3][0] & A[2..3][1])

	vld1.64	d25, [r2:64]!		@ Iota[i++]
	vbic	d26, d22, d21
	vbic	d27, d23, d22
	vld1.64	{q0}, [r0:64]		@ restore A[0..1][0]
	veor	d20, d20, d26		@ A[4][0] ^= (~A[4][1] & A[4][2])
	vbic	d26, d24, d23
	veor	d21, d21, d27		@ A[4][1] ^= (~A[4][2] & A[4][3])
	vbic	d27, d28, d24
	veor	d22, d22, d26		@ A[4][2] ^= (~A[4][3] & A[4][4])
	vbic	d26, d29, d28
	veor	d23, d23, d27		@ A[4][3] ^= (~A[4][4] & A[4][0])
	veor	d0,  d0,  d25		@ A[0][0] ^= Iota[i]
	veor	d24, d24, d26		@ A[4][4] ^= (~A[4][0] & A[4][1])

	subs	r3, r3, #1
	bne	.Loop_neon

.word	0xe12fff1e
.size	KeccakF1600_neon,.-KeccakF1600_neon

.globl	SHA3_absorb_neon
.type	SHA3_absorb_neon, %function
.align	5
SHA3_absorb_neon:
	stmdb	sp!, {r4,r5,r6,lr}
	vstmdb	sp!, {d8,d9,d10,d11,d12,d13,d14,d15}

	mov	r4, r1			@ inp
	mov	r5, r2			@ len
	mov	r6, r3			@ bsz

	vld1.32	{d0}, [r0:64]!		@ A[0][0]
	vld1.32	{d2}, [r0:64]!		@ A[0][1]
	vld1.32	{d4}, [r0:64]!		@ A[0][2]
	vld1.32	{d6}, [r0:64]!		@ A[0][3]
	vld1.32	{d8}, [r0:64]!		@ A[0][4]

	vld1.32	{d1}, [r0:64]!		@ A[1][0]
	vld1.32	{d3}, [r0:64]!		@ A[1][1]
	vld1.32	{d5}, [r0:64]!		@ A[1][2]
	vld1.32	{d7}, [r0:64]!		@ A[1][3]
	vld1.32	{d9}, [r0:64]!		@ A[1][4]

	vld1.32	{d10}, [r0:64]!		@ A[2][0]
	vld1.32	{d12}, [r0:64]!		@ A[2][1]
	vld1.32	{d14}, [r0:64]!		@ A[2][2]
	vld1.32	{d16}, [r0:64]!		@ A[2][3]
	vld1.32	{d18}, [r0:64]!		@ A[2][4]

	vld1.32	{d11}, [r0:64]!		@ A[3][0]
	vld1.32	{d13}, [r0:64]!		@ A[3][1]
	vld1.32	{d15}, [r0:64]!		@ A[3][2]
	vld1.32	{d17}, [r0:64]!		@ A[3][3]
	vld1.32	{d19}, [r0:64]!		@ A[3][4]

	vld1.32	{d20,d21,d22,d23}, [r0:64]!	@ A[4][0..3]
	vld1.32	{d24}, [r0:64]		@ A[4][4]
	sub	r0, r0, #24*8		@ rewind
	b	.Loop_absorb_neon

.align	4
.Loop_absorb_neon:
	subs	r12, r5, r6		@ len - bsz
	blo	.Labsorbed_neon
	mov	r5, r12

	vld1.8	{d31}, [r4]!		@ endian-neutral loads...
	cmp	r6, #8*2
	veor	d0, d0, d31		@ A[0][0] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d2, d2, d31		@ A[0][1] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*4
	veor	d4, d4, d31		@ A[0][2] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d6, d6, d31		@ A[0][3] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31},[r4]!
	cmp	r6, #8*6
	veor	d8, d8, d31		@ A[0][4] ^= *inp++
	blo	.Lprocess_neon

	vld1.8	{d31}, [r4]!
	veor	d1, d1, d31		@ A[1][0] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*8
	veor	d3, d3, d31		@ A[1][1] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d5, d5, d31		@ A[1][2] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*10
	veor	d7, d7, d31		@ A[1][3] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d9, d9, d31		@ A[1][4] ^= *inp++
	beq	.Lprocess_neon

	vld1.8	{d31}, [r4]!
	cmp	r6, #8*12
	veor	d10, d10, d31		@ A[2][0] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d12, d12, d31		@ A[2][1] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*14
	veor	d14, d14, d31		@ A[2][2] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d16, d16, d31		@ A[2][3] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*16
	veor	d18, d18, d31		@ A[2][4] ^= *inp++
	blo	.Lprocess_neon

	vld1.8	{d31}, [r4]!
	veor	d11, d11, d31		@ A[3][0] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*18
	veor	d13, d13, d31		@ A[3][1] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d15, d15, d31		@ A[3][2] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*20
	veor	d17, d17, d31		@ A[3][3] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d19, d19, d31		@ A[3][4] ^= *inp++
	beq	.Lprocess_neon

	vld1.8	{d31}, [r4]!
	cmp	r6, #8*22
	veor	d20, d20, d31		@ A[4][0] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d21, d21, d31		@ A[4][1] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*24
	veor	d22, d22, d31		@ A[4][2] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d23, d23, d31		@ A[4][3] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d24, d24, d31		@ A[4][4] ^= *inp++

.Lprocess_neon:
	bl	KeccakF1600_neon
	b	.Loop_absorb_neon

.align	4
.Labsorbed_neon:
	vst1.32	{d0}, [r0:64]!		@ A[0][0..4]
	vst1.32	{d2}, [r0:64]!
	vst1.32	{d4}, [r0:64]!
	vst1.32	{d6}, [r0:64]!
	vst1.32	{d8}, [r0:64]!

	vst1.32	{d1}, [r0:64]!		@ A[1][0..4]
	vst1.32	{d3}, [r0:64]!
	vst1.32	{d5}, [r0:64]!
	vst1.32	{d7}, [r0:64]!
	vst1.32	{d9}, [r0:64]!

	vst1.32	{d10}, [r0:64]!		@ A[2][0..4]
	vst1.32	{d12}, [r0:64]!
	vst1.32	{d14}, [r0:64]!
	vst1.32	{d16}, [r0:64]!
	vst1.32	{d18}, [r0:64]!

	vst1.32	{d11}, [r0:64]!		@ A[3][0..4]
	vst1.32	{d13}, [r0:64]!
	vst1.32	{d15}, [r0:64]!
	vst1.32	{d17}, [r0:64]!
	vst1.32	{d19}, [r0:64]!

	vst1.32	{d20,d21,d22,d23}, [r0:64]!	@ A[4][0..4]
	vst1.32	{d24}, [r0:64]

	mov	r0, r5			@ return value
	vldmia	sp!, {d8,d9,d10,d11,d12,d13,d14,d15}
	ldmia	sp!, {r4,r5,r6,pc}
.size	SHA3_absorb_neon,.-SHA3_absorb_neon

.globl	SHA3_squeeze_neon
.type	SHA3_squeeze_neon, %function
.align	5
SHA3_squeeze_neon:
	stmdb	sp!, {r4,r5,r6,lr}

	mov	r4, r1			@ out
	mov	r5, r2			@ len
	mov	r6, r3			@ bsz
	mov	r12, r0			@ A_flat
	mov	r14, r3			@ bsz
	b	.Loop_squeeze_neon

.align	4
.Loop_squeeze_neon:
	cmp	r5, #8
	blo	.Lsqueeze_neon_tail
	vld1.32	{d0}, [r12]!
	vst1.8	{d0}, [r4]!		@ endian-neutral store

	subs	r5, r5, #8		@ len -= 8
	beq	.Lsqueeze_neon_done

	subs	r14, r14, #8		@ bsz -= 8
	bhi	.Loop_squeeze_neon

	vstmdb	sp!,  {d8,d9,d10,d11,d12,d13,d14,d15}

	vld1.32	{d0}, [r0:64]!		@ A[0][0..4]
	vld1.32	{d2}, [r0:64]!
	vld1.32	{d4}, [r0:64]!
	vld1.32	{d6}, [r0:64]!
	vld1.32	{d8}, [r0:64]!

	vld1.32	{d1}, [r0:64]!		@ A[1][0..4]
	vld1.32	{d3}, [r0:64]!
	vld1.32	{d5}, [r0:64]!
	vld1.32	{d7}, [r0:64]!
	vld1.32	{d9}, [r0:64]!

	vld1.32	{d10}, [r0:64]!		@ A[2][0..4]
	vld1.32	{d12}, [r0:64]!
	vld1.32	{d14}, [r0:64]!
	vld1.32	{d16}, [r0:64]!
	vld1.32	{d18}, [r0:64]!

	vld1.32	{d11}, [r0:64]!		@ A[3][0..4]
	vld1.32	{d13}, [r0:64]!
	vld1.32	{d15}, [r0:64]!
	vld1.32	{d17}, [r0:64]!
	vld1.32	{d19}, [r0:64]!

	vld1.32	{d20,d21,d22,d23}, [r0:64]!	@ A[4][0..4]
	vld1.32	{d24}, [r0:64]
	sub	r0, r0, #24*8		@ rewind

	bl	KeccakF1600_neon

	mov	r12, r0			@ A_flat
	vst1.32	{d0}, [r0:64]!		@ A[0][0..4]
	vst1.32	{d2}, [r0:64]!
	vst1.32	{d4}, [r0:64]!
	vst1.32	{d6}, [r0:64]!
	vst1.32	{d8}, [r0:64]!

	vst1.32	{d1}, [r0:64]!		@ A[1][0..4]
	vst1.32	{d3}, [r0:64]!
	vst1.32	{d5}, [r0:64]!
	vst1.32	{d7}, [r0:64]!
	vst1.32	{d9}, [r0:64]!

	vst1.32	{d10}, [r0:64]!		@ A[2][0..4]
	vst1.32	{d12}, [r0:64]!
	vst1.32	{d14}, [r0:64]!
	vst1.32	{d16}, [r0:64]!
	vst1.32	{d18}, [r0:64]!

	vst1.32	{d11}, [r0:64]!		@ A[3][0..4]
	vst1.32	{d13}, [r0:64]!
	vst1.32	{d15}, [r0:64]!
	vst1.32	{d17}, [r0:64]!
	vst1.32	{d19}, [r0:64]!

	vst1.32	{d20,d21,d22,d23}, [r0:64]!	@ A[4][0..4]
	mov	r14, r6			@ bsz
	vst1.32	{d24}, [r0:64]
	mov	r0,  r12		@ rewind

	vldmia	sp!, {d8,d9,d10,d11,d12,d13,d14,d15}
	b	.Loop_squeeze_neon

.align	4
.Lsqueeze_neon_tail:
	ldmia	r12, {r2,r3}
	cmp	r5, #2
	strb	r2, [r4],#1		@ endian-neutral store
	mov	r2, r2, lsr#8
	blo	.Lsqueeze_neon_done
	strb	r2, [r4], #1
	mov	r2, r2, lsr#8
	beq	.Lsqueeze_neon_done
	strb	r2, [r4], #1
	mov	r2, r2, lsr#8
	cmp	r5, #4
	blo	.Lsqueeze_neon_done
	strb	r2, [r4], #1
	beq	.Lsqueeze_neon_done

	strb	r3, [r4], #1
	mov	r3, r3, lsr#8
	cmp	r5, #6
	blo	.Lsqueeze_neon_done
	strb	r3, [r4], #1
	mov	r3, r3, lsr#8
	beq	.Lsqueeze_neon_done
	strb	r3, [r4], #1

.Lsqueeze_neon_done:
	ldmia	sp!, {r4,r5,r6,pc}
.size	SHA3_squeeze_neon,.-SHA3_squeeze_neon
#endif
.byte	75,101,99,99,97,107,45,49,54,48,48,32,97,98,115,111,114,98,32,97,110,100,32,115,113,117,101,101,122,101,32,102,111,114,32,65,82,77,118,52,47,78,69,79,78,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	2
