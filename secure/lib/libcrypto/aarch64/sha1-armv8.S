/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from sha1-armv8.pl. */
#include "arm_arch.h"

.text


.globl	sha1_block_data_order
.type	sha1_block_data_order,%function
.align	6
sha1_block_data_order:
#ifdef	__ILP32__
	ldrsw	x16,.LOPENSSL_armcap_P
#else
	ldr	x16,.LOPENSSL_armcap_P
#endif
	adr	x17,.LOPENSSL_armcap_P
	add	x16,x16,x17
	ldr	w16,[x16]
	tst	w16,#ARMV8_SHA1
	b.ne	.Lv8_entry

	stp	x29,x30,[sp,#-96]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]

	ldp	w20,w21,[x0]
	ldp	w22,w23,[x0,#8]
	ldr	w24,[x0,#16]

.Loop:
	ldr	x3,[x1],#64
	movz	w28,#0x7999
	sub	x2,x2,#1
	movk	w28,#0x5a82,lsl#16
#ifdef	__ARMEB__
	ror	x3,x3,#32
#else
	rev32	x3,x3
#endif
	add	w24,w24,w28		// warm it up
	add	w24,w24,w3
	lsr	x4,x3,#32
	ldr	x5,[x1,#-56]
	bic	w25,w23,w21
	and	w26,w22,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	orr	w25,w25,w26
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	add	w23,w23,w4	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
#ifdef	__ARMEB__
	ror	x5,x5,#32
#else
	rev32	x5,x5
#endif
	bic	w25,w22,w20
	and	w26,w21,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	orr	w25,w25,w26
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	add	w22,w22,w5	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	lsr	x6,x5,#32
	ldr	x7,[x1,#-48]
	bic	w25,w21,w24
	and	w26,w20,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	orr	w25,w25,w26
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	add	w21,w21,w6	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
#ifdef	__ARMEB__
	ror	x7,x7,#32
#else
	rev32	x7,x7
#endif
	bic	w25,w20,w23
	and	w26,w24,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	orr	w25,w25,w26
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	add	w20,w20,w7	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	lsr	x8,x7,#32
	ldr	x9,[x1,#-40]
	bic	w25,w24,w22
	and	w26,w23,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	orr	w25,w25,w26
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	add	w24,w24,w8	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
#ifdef	__ARMEB__
	ror	x9,x9,#32
#else
	rev32	x9,x9
#endif
	bic	w25,w23,w21
	and	w26,w22,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	orr	w25,w25,w26
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	add	w23,w23,w9	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	lsr	x10,x9,#32
	ldr	x11,[x1,#-32]
	bic	w25,w22,w20
	and	w26,w21,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	orr	w25,w25,w26
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	add	w22,w22,w10	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
#ifdef	__ARMEB__
	ror	x11,x11,#32
#else
	rev32	x11,x11
#endif
	bic	w25,w21,w24
	and	w26,w20,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	orr	w25,w25,w26
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	add	w21,w21,w11	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	lsr	x12,x11,#32
	ldr	x13,[x1,#-24]
	bic	w25,w20,w23
	and	w26,w24,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	orr	w25,w25,w26
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	add	w20,w20,w12	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
#ifdef	__ARMEB__
	ror	x13,x13,#32
#else
	rev32	x13,x13
#endif
	bic	w25,w24,w22
	and	w26,w23,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	orr	w25,w25,w26
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	add	w24,w24,w13	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	lsr	x14,x13,#32
	ldr	x15,[x1,#-16]
	bic	w25,w23,w21
	and	w26,w22,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	orr	w25,w25,w26
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	add	w23,w23,w14	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
#ifdef	__ARMEB__
	ror	x15,x15,#32
#else
	rev32	x15,x15
#endif
	bic	w25,w22,w20
	and	w26,w21,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	orr	w25,w25,w26
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	add	w22,w22,w15	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	lsr	x16,x15,#32
	ldr	x17,[x1,#-8]
	bic	w25,w21,w24
	and	w26,w20,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	orr	w25,w25,w26
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	add	w21,w21,w16	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
#ifdef	__ARMEB__
	ror	x17,x17,#32
#else
	rev32	x17,x17
#endif
	bic	w25,w20,w23
	and	w26,w24,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	orr	w25,w25,w26
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	add	w20,w20,w17	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	lsr	x19,x17,#32
	eor	w3,w3,w5
	bic	w25,w24,w22
	and	w26,w23,w22
	ror	w27,w21,#27
	eor	w3,w3,w11
	add	w24,w24,w28		// future e+=K
	orr	w25,w25,w26
	add	w20,w20,w27		// e+=rot(a,5)
	eor	w3,w3,w16
	ror	w22,w22,#2
	add	w24,w24,w19	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w3,w3,#31
	eor	w4,w4,w6
	bic	w25,w23,w21
	and	w26,w22,w21
	ror	w27,w20,#27
	eor	w4,w4,w12
	add	w23,w23,w28		// future e+=K
	orr	w25,w25,w26
	add	w24,w24,w27		// e+=rot(a,5)
	eor	w4,w4,w17
	ror	w21,w21,#2
	add	w23,w23,w3	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w4,w4,#31
	eor	w5,w5,w7
	bic	w25,w22,w20
	and	w26,w21,w20
	ror	w27,w24,#27
	eor	w5,w5,w13
	add	w22,w22,w28		// future e+=K
	orr	w25,w25,w26
	add	w23,w23,w27		// e+=rot(a,5)
	eor	w5,w5,w19
	ror	w20,w20,#2
	add	w22,w22,w4	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w5,w5,#31
	eor	w6,w6,w8
	bic	w25,w21,w24
	and	w26,w20,w24
	ror	w27,w23,#27
	eor	w6,w6,w14
	add	w21,w21,w28		// future e+=K
	orr	w25,w25,w26
	add	w22,w22,w27		// e+=rot(a,5)
	eor	w6,w6,w3
	ror	w24,w24,#2
	add	w21,w21,w5	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w6,w6,#31
	eor	w7,w7,w9
	bic	w25,w20,w23
	and	w26,w24,w23
	ror	w27,w22,#27
	eor	w7,w7,w15
	add	w20,w20,w28		// future e+=K
	orr	w25,w25,w26
	add	w21,w21,w27		// e+=rot(a,5)
	eor	w7,w7,w4
	ror	w23,w23,#2
	add	w20,w20,w6	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w7,w7,#31
	movz	w28,#0xeba1
	movk	w28,#0x6ed9,lsl#16
	eor	w8,w8,w10
	bic	w25,w24,w22
	and	w26,w23,w22
	ror	w27,w21,#27
	eor	w8,w8,w16
	add	w24,w24,w28		// future e+=K
	orr	w25,w25,w26
	add	w20,w20,w27		// e+=rot(a,5)
	eor	w8,w8,w5
	ror	w22,w22,#2
	add	w24,w24,w7	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w8,w8,#31
	eor	w9,w9,w11
	eor	w25,w23,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	eor	w9,w9,w17
	eor	w25,w25,w22
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	eor	w9,w9,w6
	add	w23,w23,w8	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w9,w9,#31
	eor	w10,w10,w12
	eor	w25,w22,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	eor	w10,w10,w19
	eor	w25,w25,w21
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	eor	w10,w10,w7
	add	w22,w22,w9	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w10,w10,#31
	eor	w11,w11,w13
	eor	w25,w21,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	eor	w11,w11,w3
	eor	w25,w25,w20
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	eor	w11,w11,w8
	add	w21,w21,w10	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w11,w11,#31
	eor	w12,w12,w14
	eor	w25,w20,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	eor	w12,w12,w4
	eor	w25,w25,w24
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	eor	w12,w12,w9
	add	w20,w20,w11	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w12,w12,#31
	eor	w13,w13,w15
	eor	w25,w24,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	eor	w13,w13,w5
	eor	w25,w25,w23
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	eor	w13,w13,w10
	add	w24,w24,w12	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w13,w13,#31
	eor	w14,w14,w16
	eor	w25,w23,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	eor	w14,w14,w6
	eor	w25,w25,w22
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	eor	w14,w14,w11
	add	w23,w23,w13	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w14,w14,#31
	eor	w15,w15,w17
	eor	w25,w22,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	eor	w15,w15,w7
	eor	w25,w25,w21
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	eor	w15,w15,w12
	add	w22,w22,w14	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w15,w15,#31
	eor	w16,w16,w19
	eor	w25,w21,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	eor	w16,w16,w8
	eor	w25,w25,w20
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	eor	w16,w16,w13
	add	w21,w21,w15	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w16,w16,#31
	eor	w17,w17,w3
	eor	w25,w20,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	eor	w17,w17,w9
	eor	w25,w25,w24
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	eor	w17,w17,w14
	add	w20,w20,w16	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w17,w17,#31
	eor	w19,w19,w4
	eor	w25,w24,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	eor	w19,w19,w10
	eor	w25,w25,w23
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	eor	w19,w19,w15
	add	w24,w24,w17	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w19,w19,#31
	eor	w3,w3,w5
	eor	w25,w23,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	eor	w3,w3,w11
	eor	w25,w25,w22
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	eor	w3,w3,w16
	add	w23,w23,w19	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w3,w3,#31
	eor	w4,w4,w6
	eor	w25,w22,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	eor	w4,w4,w12
	eor	w25,w25,w21
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	eor	w4,w4,w17
	add	w22,w22,w3	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w4,w4,#31
	eor	w5,w5,w7
	eor	w25,w21,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	eor	w5,w5,w13
	eor	w25,w25,w20
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	eor	w5,w5,w19
	add	w21,w21,w4	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w5,w5,#31
	eor	w6,w6,w8
	eor	w25,w20,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	eor	w6,w6,w14
	eor	w25,w25,w24
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	eor	w6,w6,w3
	add	w20,w20,w5	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w6,w6,#31
	eor	w7,w7,w9
	eor	w25,w24,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	eor	w7,w7,w15
	eor	w25,w25,w23
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	eor	w7,w7,w4
	add	w24,w24,w6	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w7,w7,#31
	eor	w8,w8,w10
	eor	w25,w23,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	eor	w8,w8,w16
	eor	w25,w25,w22
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	eor	w8,w8,w5
	add	w23,w23,w7	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w8,w8,#31
	eor	w9,w9,w11
	eor	w25,w22,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	eor	w9,w9,w17
	eor	w25,w25,w21
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	eor	w9,w9,w6
	add	w22,w22,w8	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w9,w9,#31
	eor	w10,w10,w12
	eor	w25,w21,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	eor	w10,w10,w19
	eor	w25,w25,w20
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	eor	w10,w10,w7
	add	w21,w21,w9	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w10,w10,#31
	eor	w11,w11,w13
	eor	w25,w20,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	eor	w11,w11,w3
	eor	w25,w25,w24
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	eor	w11,w11,w8
	add	w20,w20,w10	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w11,w11,#31
	movz	w28,#0xbcdc
	movk	w28,#0x8f1b,lsl#16
	eor	w12,w12,w14
	eor	w25,w24,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	eor	w12,w12,w4
	eor	w25,w25,w23
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	eor	w12,w12,w9
	add	w24,w24,w11	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w12,w12,#31
	orr	w25,w21,w22
	and	w26,w21,w22
	eor	w13,w13,w15
	ror	w27,w20,#27
	and	w25,w25,w23
	add	w23,w23,w28		// future e+=K
	eor	w13,w13,w5
	add	w24,w24,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w21,w21,#2
	eor	w13,w13,w10
	add	w23,w23,w12	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w13,w13,#31
	orr	w25,w20,w21
	and	w26,w20,w21
	eor	w14,w14,w16
	ror	w27,w24,#27
	and	w25,w25,w22
	add	w22,w22,w28		// future e+=K
	eor	w14,w14,w6
	add	w23,w23,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w20,w20,#2
	eor	w14,w14,w11
	add	w22,w22,w13	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w14,w14,#31
	orr	w25,w24,w20
	and	w26,w24,w20
	eor	w15,w15,w17
	ror	w27,w23,#27
	and	w25,w25,w21
	add	w21,w21,w28		// future e+=K
	eor	w15,w15,w7
	add	w22,w22,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w24,w24,#2
	eor	w15,w15,w12
	add	w21,w21,w14	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w15,w15,#31
	orr	w25,w23,w24
	and	w26,w23,w24
	eor	w16,w16,w19
	ror	w27,w22,#27
	and	w25,w25,w20
	add	w20,w20,w28		// future e+=K
	eor	w16,w16,w8
	add	w21,w21,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w23,w23,#2
	eor	w16,w16,w13
	add	w20,w20,w15	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w16,w16,#31
	orr	w25,w22,w23
	and	w26,w22,w23
	eor	w17,w17,w3
	ror	w27,w21,#27
	and	w25,w25,w24
	add	w24,w24,w28		// future e+=K
	eor	w17,w17,w9
	add	w20,w20,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w22,w22,#2
	eor	w17,w17,w14
	add	w24,w24,w16	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w17,w17,#31
	orr	w25,w21,w22
	and	w26,w21,w22
	eor	w19,w19,w4
	ror	w27,w20,#27
	and	w25,w25,w23
	add	w23,w23,w28		// future e+=K
	eor	w19,w19,w10
	add	w24,w24,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w21,w21,#2
	eor	w19,w19,w15
	add	w23,w23,w17	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w19,w19,#31
	orr	w25,w20,w21
	and	w26,w20,w21
	eor	w3,w3,w5
	ror	w27,w24,#27
	and	w25,w25,w22
	add	w22,w22,w28		// future e+=K
	eor	w3,w3,w11
	add	w23,w23,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w20,w20,#2
	eor	w3,w3,w16
	add	w22,w22,w19	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w3,w3,#31
	orr	w25,w24,w20
	and	w26,w24,w20
	eor	w4,w4,w6
	ror	w27,w23,#27
	and	w25,w25,w21
	add	w21,w21,w28		// future e+=K
	eor	w4,w4,w12
	add	w22,w22,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w24,w24,#2
	eor	w4,w4,w17
	add	w21,w21,w3	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w4,w4,#31
	orr	w25,w23,w24
	and	w26,w23,w24
	eor	w5,w5,w7
	ror	w27,w22,#27
	and	w25,w25,w20
	add	w20,w20,w28		// future e+=K
	eor	w5,w5,w13
	add	w21,w21,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w23,w23,#2
	eor	w5,w5,w19
	add	w20,w20,w4	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w5,w5,#31
	orr	w25,w22,w23
	and	w26,w22,w23
	eor	w6,w6,w8
	ror	w27,w21,#27
	and	w25,w25,w24
	add	w24,w24,w28		// future e+=K
	eor	w6,w6,w14
	add	w20,w20,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w22,w22,#2
	eor	w6,w6,w3
	add	w24,w24,w5	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w6,w6,#31
	orr	w25,w21,w22
	and	w26,w21,w22
	eor	w7,w7,w9
	ror	w27,w20,#27
	and	w25,w25,w23
	add	w23,w23,w28		// future e+=K
	eor	w7,w7,w15
	add	w24,w24,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w21,w21,#2
	eor	w7,w7,w4
	add	w23,w23,w6	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w7,w7,#31
	orr	w25,w20,w21
	and	w26,w20,w21
	eor	w8,w8,w10
	ror	w27,w24,#27
	and	w25,w25,w22
	add	w22,w22,w28		// future e+=K
	eor	w8,w8,w16
	add	w23,w23,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w20,w20,#2
	eor	w8,w8,w5
	add	w22,w22,w7	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w8,w8,#31
	orr	w25,w24,w20
	and	w26,w24,w20
	eor	w9,w9,w11
	ror	w27,w23,#27
	and	w25,w25,w21
	add	w21,w21,w28		// future e+=K
	eor	w9,w9,w17
	add	w22,w22,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w24,w24,#2
	eor	w9,w9,w6
	add	w21,w21,w8	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w9,w9,#31
	orr	w25,w23,w24
	and	w26,w23,w24
	eor	w10,w10,w12
	ror	w27,w22,#27
	and	w25,w25,w20
	add	w20,w20,w28		// future e+=K
	eor	w10,w10,w19
	add	w21,w21,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w23,w23,#2
	eor	w10,w10,w7
	add	w20,w20,w9	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w10,w10,#31
	orr	w25,w22,w23
	and	w26,w22,w23
	eor	w11,w11,w13
	ror	w27,w21,#27
	and	w25,w25,w24
	add	w24,w24,w28		// future e+=K
	eor	w11,w11,w3
	add	w20,w20,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w22,w22,#2
	eor	w11,w11,w8
	add	w24,w24,w10	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w11,w11,#31
	orr	w25,w21,w22
	and	w26,w21,w22
	eor	w12,w12,w14
	ror	w27,w20,#27
	and	w25,w25,w23
	add	w23,w23,w28		// future e+=K
	eor	w12,w12,w4
	add	w24,w24,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w21,w21,#2
	eor	w12,w12,w9
	add	w23,w23,w11	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w12,w12,#31
	orr	w25,w20,w21
	and	w26,w20,w21
	eor	w13,w13,w15
	ror	w27,w24,#27
	and	w25,w25,w22
	add	w22,w22,w28		// future e+=K
	eor	w13,w13,w5
	add	w23,w23,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w20,w20,#2
	eor	w13,w13,w10
	add	w22,w22,w12	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w13,w13,#31
	orr	w25,w24,w20
	and	w26,w24,w20
	eor	w14,w14,w16
	ror	w27,w23,#27
	and	w25,w25,w21
	add	w21,w21,w28		// future e+=K
	eor	w14,w14,w6
	add	w22,w22,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w24,w24,#2
	eor	w14,w14,w11
	add	w21,w21,w13	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w14,w14,#31
	orr	w25,w23,w24
	and	w26,w23,w24
	eor	w15,w15,w17
	ror	w27,w22,#27
	and	w25,w25,w20
	add	w20,w20,w28		// future e+=K
	eor	w15,w15,w7
	add	w21,w21,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w23,w23,#2
	eor	w15,w15,w12
	add	w20,w20,w14	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w15,w15,#31
	movz	w28,#0xc1d6
	movk	w28,#0xca62,lsl#16
	orr	w25,w22,w23
	and	w26,w22,w23
	eor	w16,w16,w19
	ror	w27,w21,#27
	and	w25,w25,w24
	add	w24,w24,w28		// future e+=K
	eor	w16,w16,w8
	add	w20,w20,w27		// e+=rot(a,5)
	orr	w25,w25,w26
	ror	w22,w22,#2
	eor	w16,w16,w13
	add	w24,w24,w15	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w16,w16,#31
	eor	w17,w17,w3
	eor	w25,w23,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	eor	w17,w17,w9
	eor	w25,w25,w22
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	eor	w17,w17,w14
	add	w23,w23,w16	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w17,w17,#31
	eor	w19,w19,w4
	eor	w25,w22,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	eor	w19,w19,w10
	eor	w25,w25,w21
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	eor	w19,w19,w15
	add	w22,w22,w17	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w19,w19,#31
	eor	w3,w3,w5
	eor	w25,w21,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	eor	w3,w3,w11
	eor	w25,w25,w20
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	eor	w3,w3,w16
	add	w21,w21,w19	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w3,w3,#31
	eor	w4,w4,w6
	eor	w25,w20,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	eor	w4,w4,w12
	eor	w25,w25,w24
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	eor	w4,w4,w17
	add	w20,w20,w3	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w4,w4,#31
	eor	w5,w5,w7
	eor	w25,w24,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	eor	w5,w5,w13
	eor	w25,w25,w23
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	eor	w5,w5,w19
	add	w24,w24,w4	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w5,w5,#31
	eor	w6,w6,w8
	eor	w25,w23,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	eor	w6,w6,w14
	eor	w25,w25,w22
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	eor	w6,w6,w3
	add	w23,w23,w5	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w6,w6,#31
	eor	w7,w7,w9
	eor	w25,w22,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	eor	w7,w7,w15
	eor	w25,w25,w21
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	eor	w7,w7,w4
	add	w22,w22,w6	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w7,w7,#31
	eor	w8,w8,w10
	eor	w25,w21,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	eor	w8,w8,w16
	eor	w25,w25,w20
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	eor	w8,w8,w5
	add	w21,w21,w7	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w8,w8,#31
	eor	w9,w9,w11
	eor	w25,w20,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	eor	w9,w9,w17
	eor	w25,w25,w24
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	eor	w9,w9,w6
	add	w20,w20,w8	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w9,w9,#31
	eor	w10,w10,w12
	eor	w25,w24,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	eor	w10,w10,w19
	eor	w25,w25,w23
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	eor	w10,w10,w7
	add	w24,w24,w9	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w10,w10,#31
	eor	w11,w11,w13
	eor	w25,w23,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	eor	w11,w11,w3
	eor	w25,w25,w22
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	eor	w11,w11,w8
	add	w23,w23,w10	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w11,w11,#31
	eor	w12,w12,w14
	eor	w25,w22,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	eor	w12,w12,w4
	eor	w25,w25,w21
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	eor	w12,w12,w9
	add	w22,w22,w11	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w12,w12,#31
	eor	w13,w13,w15
	eor	w25,w21,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	eor	w13,w13,w5
	eor	w25,w25,w20
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	eor	w13,w13,w10
	add	w21,w21,w12	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w13,w13,#31
	eor	w14,w14,w16
	eor	w25,w20,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	eor	w14,w14,w6
	eor	w25,w25,w24
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	eor	w14,w14,w11
	add	w20,w20,w13	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ror	w14,w14,#31
	eor	w15,w15,w17
	eor	w25,w24,w22
	ror	w27,w21,#27
	add	w24,w24,w28		// future e+=K
	eor	w15,w15,w7
	eor	w25,w25,w23
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	eor	w15,w15,w12
	add	w24,w24,w14	// future e+=X[i]
	add	w20,w20,w25		// e+=F(b,c,d)
	ror	w15,w15,#31
	eor	w16,w16,w19
	eor	w25,w23,w21
	ror	w27,w20,#27
	add	w23,w23,w28		// future e+=K
	eor	w16,w16,w8
	eor	w25,w25,w22
	add	w24,w24,w27		// e+=rot(a,5)
	ror	w21,w21,#2
	eor	w16,w16,w13
	add	w23,w23,w15	// future e+=X[i]
	add	w24,w24,w25		// e+=F(b,c,d)
	ror	w16,w16,#31
	eor	w17,w17,w3
	eor	w25,w22,w20
	ror	w27,w24,#27
	add	w22,w22,w28		// future e+=K
	eor	w17,w17,w9
	eor	w25,w25,w21
	add	w23,w23,w27		// e+=rot(a,5)
	ror	w20,w20,#2
	eor	w17,w17,w14
	add	w22,w22,w16	// future e+=X[i]
	add	w23,w23,w25		// e+=F(b,c,d)
	ror	w17,w17,#31
	eor	w19,w19,w4
	eor	w25,w21,w24
	ror	w27,w23,#27
	add	w21,w21,w28		// future e+=K
	eor	w19,w19,w10
	eor	w25,w25,w20
	add	w22,w22,w27		// e+=rot(a,5)
	ror	w24,w24,#2
	eor	w19,w19,w15
	add	w21,w21,w17	// future e+=X[i]
	add	w22,w22,w25		// e+=F(b,c,d)
	ror	w19,w19,#31
	ldp	w4,w5,[x0]
	eor	w25,w20,w23
	ror	w27,w22,#27
	add	w20,w20,w28		// future e+=K
	eor	w25,w25,w24
	add	w21,w21,w27		// e+=rot(a,5)
	ror	w23,w23,#2
	add	w20,w20,w19	// future e+=X[i]
	add	w21,w21,w25		// e+=F(b,c,d)
	ldp	w6,w7,[x0,#8]
	eor	w25,w24,w22
	ror	w27,w21,#27
	eor	w25,w25,w23
	add	w20,w20,w27		// e+=rot(a,5)
	ror	w22,w22,#2
	ldr	w8,[x0,#16]
	add	w20,w20,w25		// e+=F(b,c,d)
	add	w21,w21,w5
	add	w22,w22,w6
	add	w20,w20,w4
	add	w23,w23,w7
	add	w24,w24,w8
	stp	w20,w21,[x0]
	stp	w22,w23,[x0,#8]
	str	w24,[x0,#16]
	cbnz	x2,.Loop

	ldp	x19,x20,[sp,#16]
	ldp	x21,x22,[sp,#32]
	ldp	x23,x24,[sp,#48]
	ldp	x25,x26,[sp,#64]
	ldp	x27,x28,[sp,#80]
	ldr	x29,[sp],#96
	ret
.size	sha1_block_data_order,.-sha1_block_data_order
.type	sha1_block_armv8,%function
.align	6
sha1_block_armv8:
.Lv8_entry:
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0

	adr	x4,.Lconst
	eor	v1.16b,v1.16b,v1.16b
	ld1	{v0.4s},[x0],#16
	ld1	{v1.s}[0],[x0]
	sub	x0,x0,#16
	ld1	{v16.4s,v17.4s,v18.4s,v19.4s},[x4]

.Loop_hw:
	ld1	{v4.16b,v5.16b,v6.16b,v7.16b},[x1],#64
	sub	x2,x2,#1
	rev32	v4.16b,v4.16b
	rev32	v5.16b,v5.16b

	add	v20.4s,v16.4s,v4.4s
	rev32	v6.16b,v6.16b
	orr	v22.16b,v0.16b,v0.16b	// offload

	add	v21.4s,v16.4s,v5.4s
	rev32	v7.16b,v7.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b
.inst	0x5e140020	//sha1c v0.16b,v1.16b,v20.4s		// 0
	add	v20.4s,v16.4s,v6.4s
.inst	0x5e0630a4	//sha1su0 v4.16b,v5.16b,v6.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 1
.inst	0x5e150060	//sha1c v0.16b,v3.16b,v21.4s
	add	v21.4s,v16.4s,v7.4s
.inst	0x5e2818e4	//sha1su1 v4.16b,v7.16b
.inst	0x5e0730c5	//sha1su0 v5.16b,v6.16b,v7.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 2
.inst	0x5e140040	//sha1c v0.16b,v2.16b,v20.4s
	add	v20.4s,v16.4s,v4.4s
.inst	0x5e281885	//sha1su1 v5.16b,v4.16b
.inst	0x5e0430e6	//sha1su0 v6.16b,v7.16b,v4.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 3
.inst	0x5e150060	//sha1c v0.16b,v3.16b,v21.4s
	add	v21.4s,v17.4s,v5.4s
.inst	0x5e2818a6	//sha1su1 v6.16b,v5.16b
.inst	0x5e053087	//sha1su0 v7.16b,v4.16b,v5.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 4
.inst	0x5e140040	//sha1c v0.16b,v2.16b,v20.4s
	add	v20.4s,v17.4s,v6.4s
.inst	0x5e2818c7	//sha1su1 v7.16b,v6.16b
.inst	0x5e0630a4	//sha1su0 v4.16b,v5.16b,v6.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 5
.inst	0x5e151060	//sha1p v0.16b,v3.16b,v21.4s
	add	v21.4s,v17.4s,v7.4s
.inst	0x5e2818e4	//sha1su1 v4.16b,v7.16b
.inst	0x5e0730c5	//sha1su0 v5.16b,v6.16b,v7.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 6
.inst	0x5e141040	//sha1p v0.16b,v2.16b,v20.4s
	add	v20.4s,v17.4s,v4.4s
.inst	0x5e281885	//sha1su1 v5.16b,v4.16b
.inst	0x5e0430e6	//sha1su0 v6.16b,v7.16b,v4.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 7
.inst	0x5e151060	//sha1p v0.16b,v3.16b,v21.4s
	add	v21.4s,v17.4s,v5.4s
.inst	0x5e2818a6	//sha1su1 v6.16b,v5.16b
.inst	0x5e053087	//sha1su0 v7.16b,v4.16b,v5.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 8
.inst	0x5e141040	//sha1p v0.16b,v2.16b,v20.4s
	add	v20.4s,v18.4s,v6.4s
.inst	0x5e2818c7	//sha1su1 v7.16b,v6.16b
.inst	0x5e0630a4	//sha1su0 v4.16b,v5.16b,v6.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 9
.inst	0x5e151060	//sha1p v0.16b,v3.16b,v21.4s
	add	v21.4s,v18.4s,v7.4s
.inst	0x5e2818e4	//sha1su1 v4.16b,v7.16b
.inst	0x5e0730c5	//sha1su0 v5.16b,v6.16b,v7.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 10
.inst	0x5e142040	//sha1m v0.16b,v2.16b,v20.4s
	add	v20.4s,v18.4s,v4.4s
.inst	0x5e281885	//sha1su1 v5.16b,v4.16b
.inst	0x5e0430e6	//sha1su0 v6.16b,v7.16b,v4.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 11
.inst	0x5e152060	//sha1m v0.16b,v3.16b,v21.4s
	add	v21.4s,v18.4s,v5.4s
.inst	0x5e2818a6	//sha1su1 v6.16b,v5.16b
.inst	0x5e053087	//sha1su0 v7.16b,v4.16b,v5.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 12
.inst	0x5e142040	//sha1m v0.16b,v2.16b,v20.4s
	add	v20.4s,v18.4s,v6.4s
.inst	0x5e2818c7	//sha1su1 v7.16b,v6.16b
.inst	0x5e0630a4	//sha1su0 v4.16b,v5.16b,v6.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 13
.inst	0x5e152060	//sha1m v0.16b,v3.16b,v21.4s
	add	v21.4s,v19.4s,v7.4s
.inst	0x5e2818e4	//sha1su1 v4.16b,v7.16b
.inst	0x5e0730c5	//sha1su0 v5.16b,v6.16b,v7.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 14
.inst	0x5e142040	//sha1m v0.16b,v2.16b,v20.4s
	add	v20.4s,v19.4s,v4.4s
.inst	0x5e281885	//sha1su1 v5.16b,v4.16b
.inst	0x5e0430e6	//sha1su0 v6.16b,v7.16b,v4.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 15
.inst	0x5e151060	//sha1p v0.16b,v3.16b,v21.4s
	add	v21.4s,v19.4s,v5.4s
.inst	0x5e2818a6	//sha1su1 v6.16b,v5.16b
.inst	0x5e053087	//sha1su0 v7.16b,v4.16b,v5.16b
.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 16
.inst	0x5e141040	//sha1p v0.16b,v2.16b,v20.4s
	add	v20.4s,v19.4s,v6.4s
.inst	0x5e2818c7	//sha1su1 v7.16b,v6.16b
.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 17
.inst	0x5e151060	//sha1p v0.16b,v3.16b,v21.4s
	add	v21.4s,v19.4s,v7.4s

.inst	0x5e280803	//sha1h v3.16b,v0.16b		// 18
.inst	0x5e141040	//sha1p v0.16b,v2.16b,v20.4s

.inst	0x5e280802	//sha1h v2.16b,v0.16b		// 19
.inst	0x5e151060	//sha1p v0.16b,v3.16b,v21.4s

	add	v1.4s,v1.4s,v2.4s
	add	v0.4s,v0.4s,v22.4s

	cbnz	x2,.Loop_hw

	st1	{v0.4s},[x0],#16
	st1	{v1.s}[0],[x0]

	ldr	x29,[sp],#16
	ret
.size	sha1_block_armv8,.-sha1_block_armv8
.align	6
.Lconst:
.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999	//K_00_19
.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1	//K_20_39
.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc	//K_40_59
.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6	//K_60_79
.LOPENSSL_armcap_P:
#ifdef	__ILP32__
.long	OPENSSL_armcap_P-.
#else
.quad	OPENSSL_armcap_P-.
#endif
.byte	83,72,65,49,32,98,108,111,99,107,32,116,114,97,110,115,102,111,114,109,32,102,111,114,32,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	2
.comm	OPENSSL_armcap_P,4,4
