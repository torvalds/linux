/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from sha512-armv8.pl. */
// Copyright 2014-2019 The OpenSSL Project Authors. All Rights Reserved.
//
// Licensed under the OpenSSL license (the "License").  You may not use
// this file except in compliance with the License.  You can obtain a copy
// in the file LICENSE in the source distribution or at
// https://www.openssl.org/source/license.html

// ====================================================================
// Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
// project. The module is, however, dual licensed under OpenSSL and
// CRYPTOGAMS licenses depending on where you obtain it. For further
// details see http://www.openssl.org/~appro/cryptogams/.
//
// Permission to use under GPLv2 terms is granted.
// ====================================================================
//
// SHA256/512 for ARMv8.
//
// Performance in cycles per processed byte and improvement coefficient
// over code generated with "default" compiler:
//
//		SHA256-hw	SHA256(*)	SHA512
// Apple A7	1.97		10.5 (+33%)	6.73 (-1%(**))
// Cortex-A53	2.38		15.5 (+115%)	10.0 (+150%(***))
// Cortex-A57	2.31		11.6 (+86%)	7.51 (+260%(***))
// Denver	2.01		10.5 (+26%)	6.70 (+8%)
// X-Gene			20.0 (+100%)	12.8 (+300%(***))
// Mongoose	2.36		13.0 (+50%)	8.36 (+33%)
// Kryo		1.92		17.4 (+30%)	11.2 (+8%)
//
// (*)	Software SHA256 results are of lesser relevance, presented
//	mostly for informational purposes.
// (**)	The result is a trade-off: it's possible to improve it by
//	10% (or by 1 cycle per round), but at the cost of 20% loss
//	on Cortex-A53 (or by 4 cycles per round).
// (***)	Super-impressive coefficients over gcc-generated code are
//	indication of some compiler "pathology", most notably code
//	generated with -mgeneral-regs-only is significantly faster
//	and the gap is only 40-90%.
//
// October 2016.
//
// Originally it was reckoned that it makes no sense to implement NEON
// version of SHA256 for 64-bit processors. This is because performance
// improvement on most wide-spread Cortex-A5x processors was observed
// to be marginal, same on Cortex-A53 and ~10% on A57. But then it was
// observed that 32-bit NEON SHA256 performs significantly better than
// 64-bit scalar version on *some* of the more recent processors. As
// result 64-bit NEON version of SHA256 was added to provide best
// all-round performance. For example it executes ~30% faster on X-Gene
// and Mongoose. [For reference, NEON version of SHA512 is bound to
// deliver much less improvement, likely *negative* on Cortex-A5x.
// Which is why NEON support is limited to SHA256.]

#ifndef	__KERNEL__
# include "arm_arch.h"
#endif

.text


.globl	sha512_block_data_order
.type	sha512_block_data_order,%function
.align	6
sha512_block_data_order:
#ifndef	__KERNEL__
# ifdef	__ILP32__
	ldrsw	x16,.LOPENSSL_armcap_P
# else
	ldr	x16,.LOPENSSL_armcap_P
# endif
	adr	x17,.LOPENSSL_armcap_P
	add	x16,x16,x17
	ldr	w16,[x16]
	tst	w16,#ARMV8_SHA512
	b.ne	.Lv8_entry
#endif
.inst	0xd503233f				// paciasp
	stp	x29,x30,[sp,#-128]!
	add	x29,sp,#0

	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]
	sub	sp,sp,#4*8

	ldp	x20,x21,[x0]				// load context
	ldp	x22,x23,[x0,#2*8]
	ldp	x24,x25,[x0,#4*8]
	add	x2,x1,x2,lsl#7	// end of input
	ldp	x26,x27,[x0,#6*8]
	adr	x30,.LK512
	stp	x0,x2,[x29,#96]

.Loop:
	ldp	x3,x4,[x1],#2*8
	ldr	x19,[x30],#8			// *K++
	eor	x28,x21,x22				// magic seed
	str	x1,[x29,#112]
#ifndef	__AARCH64EB__
	rev	x3,x3			// 0
#endif
	ror	x16,x24,#14
	add	x27,x27,x19			// h+=K[i]
	eor	x6,x24,x24,ror#23
	and	x17,x25,x24
	bic	x19,x26,x24
	add	x27,x27,x3			// h+=X[i]
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x20,x21			// a^b, b^c in next round
	eor	x16,x16,x6,ror#18	// Sigma1(e)
	ror	x6,x20,#28
	add	x27,x27,x17			// h+=Ch(e,f,g)
	eor	x17,x20,x20,ror#5
	add	x27,x27,x16			// h+=Sigma1(e)
	and	x28,x28,x19			// (b^c)&=(a^b)
	add	x23,x23,x27			// d+=h
	eor	x28,x28,x21			// Maj(a,b,c)
	eor	x17,x6,x17,ror#34	// Sigma0(a)
	add	x27,x27,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	//add	x27,x27,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x4,x4			// 1
#endif
	ldp	x5,x6,[x1],#2*8
	add	x27,x27,x17			// h+=Sigma0(a)
	ror	x16,x23,#14
	add	x26,x26,x28			// h+=K[i]
	eor	x7,x23,x23,ror#23
	and	x17,x24,x23
	bic	x28,x25,x23
	add	x26,x26,x4			// h+=X[i]
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x27,x20			// a^b, b^c in next round
	eor	x16,x16,x7,ror#18	// Sigma1(e)
	ror	x7,x27,#28
	add	x26,x26,x17			// h+=Ch(e,f,g)
	eor	x17,x27,x27,ror#5
	add	x26,x26,x16			// h+=Sigma1(e)
	and	x19,x19,x28			// (b^c)&=(a^b)
	add	x22,x22,x26			// d+=h
	eor	x19,x19,x20			// Maj(a,b,c)
	eor	x17,x7,x17,ror#34	// Sigma0(a)
	add	x26,x26,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	//add	x26,x26,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x5,x5			// 2
#endif
	add	x26,x26,x17			// h+=Sigma0(a)
	ror	x16,x22,#14
	add	x25,x25,x19			// h+=K[i]
	eor	x8,x22,x22,ror#23
	and	x17,x23,x22
	bic	x19,x24,x22
	add	x25,x25,x5			// h+=X[i]
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x26,x27			// a^b, b^c in next round
	eor	x16,x16,x8,ror#18	// Sigma1(e)
	ror	x8,x26,#28
	add	x25,x25,x17			// h+=Ch(e,f,g)
	eor	x17,x26,x26,ror#5
	add	x25,x25,x16			// h+=Sigma1(e)
	and	x28,x28,x19			// (b^c)&=(a^b)
	add	x21,x21,x25			// d+=h
	eor	x28,x28,x27			// Maj(a,b,c)
	eor	x17,x8,x17,ror#34	// Sigma0(a)
	add	x25,x25,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	//add	x25,x25,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x6,x6			// 3
#endif
	ldp	x7,x8,[x1],#2*8
	add	x25,x25,x17			// h+=Sigma0(a)
	ror	x16,x21,#14
	add	x24,x24,x28			// h+=K[i]
	eor	x9,x21,x21,ror#23
	and	x17,x22,x21
	bic	x28,x23,x21
	add	x24,x24,x6			// h+=X[i]
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x25,x26			// a^b, b^c in next round
	eor	x16,x16,x9,ror#18	// Sigma1(e)
	ror	x9,x25,#28
	add	x24,x24,x17			// h+=Ch(e,f,g)
	eor	x17,x25,x25,ror#5
	add	x24,x24,x16			// h+=Sigma1(e)
	and	x19,x19,x28			// (b^c)&=(a^b)
	add	x20,x20,x24			// d+=h
	eor	x19,x19,x26			// Maj(a,b,c)
	eor	x17,x9,x17,ror#34	// Sigma0(a)
	add	x24,x24,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	//add	x24,x24,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x7,x7			// 4
#endif
	add	x24,x24,x17			// h+=Sigma0(a)
	ror	x16,x20,#14
	add	x23,x23,x19			// h+=K[i]
	eor	x10,x20,x20,ror#23
	and	x17,x21,x20
	bic	x19,x22,x20
	add	x23,x23,x7			// h+=X[i]
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x24,x25			// a^b, b^c in next round
	eor	x16,x16,x10,ror#18	// Sigma1(e)
	ror	x10,x24,#28
	add	x23,x23,x17			// h+=Ch(e,f,g)
	eor	x17,x24,x24,ror#5
	add	x23,x23,x16			// h+=Sigma1(e)
	and	x28,x28,x19			// (b^c)&=(a^b)
	add	x27,x27,x23			// d+=h
	eor	x28,x28,x25			// Maj(a,b,c)
	eor	x17,x10,x17,ror#34	// Sigma0(a)
	add	x23,x23,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	//add	x23,x23,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x8,x8			// 5
#endif
	ldp	x9,x10,[x1],#2*8
	add	x23,x23,x17			// h+=Sigma0(a)
	ror	x16,x27,#14
	add	x22,x22,x28			// h+=K[i]
	eor	x11,x27,x27,ror#23
	and	x17,x20,x27
	bic	x28,x21,x27
	add	x22,x22,x8			// h+=X[i]
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x23,x24			// a^b, b^c in next round
	eor	x16,x16,x11,ror#18	// Sigma1(e)
	ror	x11,x23,#28
	add	x22,x22,x17			// h+=Ch(e,f,g)
	eor	x17,x23,x23,ror#5
	add	x22,x22,x16			// h+=Sigma1(e)
	and	x19,x19,x28			// (b^c)&=(a^b)
	add	x26,x26,x22			// d+=h
	eor	x19,x19,x24			// Maj(a,b,c)
	eor	x17,x11,x17,ror#34	// Sigma0(a)
	add	x22,x22,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	//add	x22,x22,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x9,x9			// 6
#endif
	add	x22,x22,x17			// h+=Sigma0(a)
	ror	x16,x26,#14
	add	x21,x21,x19			// h+=K[i]
	eor	x12,x26,x26,ror#23
	and	x17,x27,x26
	bic	x19,x20,x26
	add	x21,x21,x9			// h+=X[i]
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x22,x23			// a^b, b^c in next round
	eor	x16,x16,x12,ror#18	// Sigma1(e)
	ror	x12,x22,#28
	add	x21,x21,x17			// h+=Ch(e,f,g)
	eor	x17,x22,x22,ror#5
	add	x21,x21,x16			// h+=Sigma1(e)
	and	x28,x28,x19			// (b^c)&=(a^b)
	add	x25,x25,x21			// d+=h
	eor	x28,x28,x23			// Maj(a,b,c)
	eor	x17,x12,x17,ror#34	// Sigma0(a)
	add	x21,x21,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	//add	x21,x21,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x10,x10			// 7
#endif
	ldp	x11,x12,[x1],#2*8
	add	x21,x21,x17			// h+=Sigma0(a)
	ror	x16,x25,#14
	add	x20,x20,x28			// h+=K[i]
	eor	x13,x25,x25,ror#23
	and	x17,x26,x25
	bic	x28,x27,x25
	add	x20,x20,x10			// h+=X[i]
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x21,x22			// a^b, b^c in next round
	eor	x16,x16,x13,ror#18	// Sigma1(e)
	ror	x13,x21,#28
	add	x20,x20,x17			// h+=Ch(e,f,g)
	eor	x17,x21,x21,ror#5
	add	x20,x20,x16			// h+=Sigma1(e)
	and	x19,x19,x28			// (b^c)&=(a^b)
	add	x24,x24,x20			// d+=h
	eor	x19,x19,x22			// Maj(a,b,c)
	eor	x17,x13,x17,ror#34	// Sigma0(a)
	add	x20,x20,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	//add	x20,x20,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x11,x11			// 8
#endif
	add	x20,x20,x17			// h+=Sigma0(a)
	ror	x16,x24,#14
	add	x27,x27,x19			// h+=K[i]
	eor	x14,x24,x24,ror#23
	and	x17,x25,x24
	bic	x19,x26,x24
	add	x27,x27,x11			// h+=X[i]
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x20,x21			// a^b, b^c in next round
	eor	x16,x16,x14,ror#18	// Sigma1(e)
	ror	x14,x20,#28
	add	x27,x27,x17			// h+=Ch(e,f,g)
	eor	x17,x20,x20,ror#5
	add	x27,x27,x16			// h+=Sigma1(e)
	and	x28,x28,x19			// (b^c)&=(a^b)
	add	x23,x23,x27			// d+=h
	eor	x28,x28,x21			// Maj(a,b,c)
	eor	x17,x14,x17,ror#34	// Sigma0(a)
	add	x27,x27,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	//add	x27,x27,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x12,x12			// 9
#endif
	ldp	x13,x14,[x1],#2*8
	add	x27,x27,x17			// h+=Sigma0(a)
	ror	x16,x23,#14
	add	x26,x26,x28			// h+=K[i]
	eor	x15,x23,x23,ror#23
	and	x17,x24,x23
	bic	x28,x25,x23
	add	x26,x26,x12			// h+=X[i]
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x27,x20			// a^b, b^c in next round
	eor	x16,x16,x15,ror#18	// Sigma1(e)
	ror	x15,x27,#28
	add	x26,x26,x17			// h+=Ch(e,f,g)
	eor	x17,x27,x27,ror#5
	add	x26,x26,x16			// h+=Sigma1(e)
	and	x19,x19,x28			// (b^c)&=(a^b)
	add	x22,x22,x26			// d+=h
	eor	x19,x19,x20			// Maj(a,b,c)
	eor	x17,x15,x17,ror#34	// Sigma0(a)
	add	x26,x26,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	//add	x26,x26,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x13,x13			// 10
#endif
	add	x26,x26,x17			// h+=Sigma0(a)
	ror	x16,x22,#14
	add	x25,x25,x19			// h+=K[i]
	eor	x0,x22,x22,ror#23
	and	x17,x23,x22
	bic	x19,x24,x22
	add	x25,x25,x13			// h+=X[i]
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x26,x27			// a^b, b^c in next round
	eor	x16,x16,x0,ror#18	// Sigma1(e)
	ror	x0,x26,#28
	add	x25,x25,x17			// h+=Ch(e,f,g)
	eor	x17,x26,x26,ror#5
	add	x25,x25,x16			// h+=Sigma1(e)
	and	x28,x28,x19			// (b^c)&=(a^b)
	add	x21,x21,x25			// d+=h
	eor	x28,x28,x27			// Maj(a,b,c)
	eor	x17,x0,x17,ror#34	// Sigma0(a)
	add	x25,x25,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	//add	x25,x25,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x14,x14			// 11
#endif
	ldp	x15,x0,[x1],#2*8
	add	x25,x25,x17			// h+=Sigma0(a)
	str	x6,[sp,#24]
	ror	x16,x21,#14
	add	x24,x24,x28			// h+=K[i]
	eor	x6,x21,x21,ror#23
	and	x17,x22,x21
	bic	x28,x23,x21
	add	x24,x24,x14			// h+=X[i]
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x25,x26			// a^b, b^c in next round
	eor	x16,x16,x6,ror#18	// Sigma1(e)
	ror	x6,x25,#28
	add	x24,x24,x17			// h+=Ch(e,f,g)
	eor	x17,x25,x25,ror#5
	add	x24,x24,x16			// h+=Sigma1(e)
	and	x19,x19,x28			// (b^c)&=(a^b)
	add	x20,x20,x24			// d+=h
	eor	x19,x19,x26			// Maj(a,b,c)
	eor	x17,x6,x17,ror#34	// Sigma0(a)
	add	x24,x24,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	//add	x24,x24,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x15,x15			// 12
#endif
	add	x24,x24,x17			// h+=Sigma0(a)
	str	x7,[sp,#0]
	ror	x16,x20,#14
	add	x23,x23,x19			// h+=K[i]
	eor	x7,x20,x20,ror#23
	and	x17,x21,x20
	bic	x19,x22,x20
	add	x23,x23,x15			// h+=X[i]
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x24,x25			// a^b, b^c in next round
	eor	x16,x16,x7,ror#18	// Sigma1(e)
	ror	x7,x24,#28
	add	x23,x23,x17			// h+=Ch(e,f,g)
	eor	x17,x24,x24,ror#5
	add	x23,x23,x16			// h+=Sigma1(e)
	and	x28,x28,x19			// (b^c)&=(a^b)
	add	x27,x27,x23			// d+=h
	eor	x28,x28,x25			// Maj(a,b,c)
	eor	x17,x7,x17,ror#34	// Sigma0(a)
	add	x23,x23,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	//add	x23,x23,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x0,x0			// 13
#endif
	ldp	x1,x2,[x1]
	add	x23,x23,x17			// h+=Sigma0(a)
	str	x8,[sp,#8]
	ror	x16,x27,#14
	add	x22,x22,x28			// h+=K[i]
	eor	x8,x27,x27,ror#23
	and	x17,x20,x27
	bic	x28,x21,x27
	add	x22,x22,x0			// h+=X[i]
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x23,x24			// a^b, b^c in next round
	eor	x16,x16,x8,ror#18	// Sigma1(e)
	ror	x8,x23,#28
	add	x22,x22,x17			// h+=Ch(e,f,g)
	eor	x17,x23,x23,ror#5
	add	x22,x22,x16			// h+=Sigma1(e)
	and	x19,x19,x28			// (b^c)&=(a^b)
	add	x26,x26,x22			// d+=h
	eor	x19,x19,x24			// Maj(a,b,c)
	eor	x17,x8,x17,ror#34	// Sigma0(a)
	add	x22,x22,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	//add	x22,x22,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x1,x1			// 14
#endif
	ldr	x6,[sp,#24]
	add	x22,x22,x17			// h+=Sigma0(a)
	str	x9,[sp,#16]
	ror	x16,x26,#14
	add	x21,x21,x19			// h+=K[i]
	eor	x9,x26,x26,ror#23
	and	x17,x27,x26
	bic	x19,x20,x26
	add	x21,x21,x1			// h+=X[i]
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x22,x23			// a^b, b^c in next round
	eor	x16,x16,x9,ror#18	// Sigma1(e)
	ror	x9,x22,#28
	add	x21,x21,x17			// h+=Ch(e,f,g)
	eor	x17,x22,x22,ror#5
	add	x21,x21,x16			// h+=Sigma1(e)
	and	x28,x28,x19			// (b^c)&=(a^b)
	add	x25,x25,x21			// d+=h
	eor	x28,x28,x23			// Maj(a,b,c)
	eor	x17,x9,x17,ror#34	// Sigma0(a)
	add	x21,x21,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	//add	x21,x21,x17			// h+=Sigma0(a)
#ifndef	__AARCH64EB__
	rev	x2,x2			// 15
#endif
	ldr	x7,[sp,#0]
	add	x21,x21,x17			// h+=Sigma0(a)
	str	x10,[sp,#24]
	ror	x16,x25,#14
	add	x20,x20,x28			// h+=K[i]
	ror	x9,x4,#1
	and	x17,x26,x25
	ror	x8,x1,#19
	bic	x28,x27,x25
	ror	x10,x21,#28
	add	x20,x20,x2			// h+=X[i]
	eor	x16,x16,x25,ror#18
	eor	x9,x9,x4,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x21,x22			// a^b, b^c in next round
	eor	x16,x16,x25,ror#41	// Sigma1(e)
	eor	x10,x10,x21,ror#34
	add	x20,x20,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x8,x8,x1,ror#61
	eor	x9,x9,x4,lsr#7	// sigma0(X[i+1])
	add	x20,x20,x16			// h+=Sigma1(e)
	eor	x19,x19,x22			// Maj(a,b,c)
	eor	x17,x10,x21,ror#39	// Sigma0(a)
	eor	x8,x8,x1,lsr#6	// sigma1(X[i+14])
	add	x3,x3,x12
	add	x24,x24,x20			// d+=h
	add	x20,x20,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x3,x3,x9
	add	x20,x20,x17			// h+=Sigma0(a)
	add	x3,x3,x8
.Loop_16_xx:
	ldr	x8,[sp,#8]
	str	x11,[sp,#0]
	ror	x16,x24,#14
	add	x27,x27,x19			// h+=K[i]
	ror	x10,x5,#1
	and	x17,x25,x24
	ror	x9,x2,#19
	bic	x19,x26,x24
	ror	x11,x20,#28
	add	x27,x27,x3			// h+=X[i]
	eor	x16,x16,x24,ror#18
	eor	x10,x10,x5,ror#8
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x20,x21			// a^b, b^c in next round
	eor	x16,x16,x24,ror#41	// Sigma1(e)
	eor	x11,x11,x20,ror#34
	add	x27,x27,x17			// h+=Ch(e,f,g)
	and	x28,x28,x19			// (b^c)&=(a^b)
	eor	x9,x9,x2,ror#61
	eor	x10,x10,x5,lsr#7	// sigma0(X[i+1])
	add	x27,x27,x16			// h+=Sigma1(e)
	eor	x28,x28,x21			// Maj(a,b,c)
	eor	x17,x11,x20,ror#39	// Sigma0(a)
	eor	x9,x9,x2,lsr#6	// sigma1(X[i+14])
	add	x4,x4,x13
	add	x23,x23,x27			// d+=h
	add	x27,x27,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	add	x4,x4,x10
	add	x27,x27,x17			// h+=Sigma0(a)
	add	x4,x4,x9
	ldr	x9,[sp,#16]
	str	x12,[sp,#8]
	ror	x16,x23,#14
	add	x26,x26,x28			// h+=K[i]
	ror	x11,x6,#1
	and	x17,x24,x23
	ror	x10,x3,#19
	bic	x28,x25,x23
	ror	x12,x27,#28
	add	x26,x26,x4			// h+=X[i]
	eor	x16,x16,x23,ror#18
	eor	x11,x11,x6,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x27,x20			// a^b, b^c in next round
	eor	x16,x16,x23,ror#41	// Sigma1(e)
	eor	x12,x12,x27,ror#34
	add	x26,x26,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x10,x10,x3,ror#61
	eor	x11,x11,x6,lsr#7	// sigma0(X[i+1])
	add	x26,x26,x16			// h+=Sigma1(e)
	eor	x19,x19,x20			// Maj(a,b,c)
	eor	x17,x12,x27,ror#39	// Sigma0(a)
	eor	x10,x10,x3,lsr#6	// sigma1(X[i+14])
	add	x5,x5,x14
	add	x22,x22,x26			// d+=h
	add	x26,x26,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x5,x5,x11
	add	x26,x26,x17			// h+=Sigma0(a)
	add	x5,x5,x10
	ldr	x10,[sp,#24]
	str	x13,[sp,#16]
	ror	x16,x22,#14
	add	x25,x25,x19			// h+=K[i]
	ror	x12,x7,#1
	and	x17,x23,x22
	ror	x11,x4,#19
	bic	x19,x24,x22
	ror	x13,x26,#28
	add	x25,x25,x5			// h+=X[i]
	eor	x16,x16,x22,ror#18
	eor	x12,x12,x7,ror#8
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x26,x27			// a^b, b^c in next round
	eor	x16,x16,x22,ror#41	// Sigma1(e)
	eor	x13,x13,x26,ror#34
	add	x25,x25,x17			// h+=Ch(e,f,g)
	and	x28,x28,x19			// (b^c)&=(a^b)
	eor	x11,x11,x4,ror#61
	eor	x12,x12,x7,lsr#7	// sigma0(X[i+1])
	add	x25,x25,x16			// h+=Sigma1(e)
	eor	x28,x28,x27			// Maj(a,b,c)
	eor	x17,x13,x26,ror#39	// Sigma0(a)
	eor	x11,x11,x4,lsr#6	// sigma1(X[i+14])
	add	x6,x6,x15
	add	x21,x21,x25			// d+=h
	add	x25,x25,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	add	x6,x6,x12
	add	x25,x25,x17			// h+=Sigma0(a)
	add	x6,x6,x11
	ldr	x11,[sp,#0]
	str	x14,[sp,#24]
	ror	x16,x21,#14
	add	x24,x24,x28			// h+=K[i]
	ror	x13,x8,#1
	and	x17,x22,x21
	ror	x12,x5,#19
	bic	x28,x23,x21
	ror	x14,x25,#28
	add	x24,x24,x6			// h+=X[i]
	eor	x16,x16,x21,ror#18
	eor	x13,x13,x8,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x25,x26			// a^b, b^c in next round
	eor	x16,x16,x21,ror#41	// Sigma1(e)
	eor	x14,x14,x25,ror#34
	add	x24,x24,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x12,x12,x5,ror#61
	eor	x13,x13,x8,lsr#7	// sigma0(X[i+1])
	add	x24,x24,x16			// h+=Sigma1(e)
	eor	x19,x19,x26			// Maj(a,b,c)
	eor	x17,x14,x25,ror#39	// Sigma0(a)
	eor	x12,x12,x5,lsr#6	// sigma1(X[i+14])
	add	x7,x7,x0
	add	x20,x20,x24			// d+=h
	add	x24,x24,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x7,x7,x13
	add	x24,x24,x17			// h+=Sigma0(a)
	add	x7,x7,x12
	ldr	x12,[sp,#8]
	str	x15,[sp,#0]
	ror	x16,x20,#14
	add	x23,x23,x19			// h+=K[i]
	ror	x14,x9,#1
	and	x17,x21,x20
	ror	x13,x6,#19
	bic	x19,x22,x20
	ror	x15,x24,#28
	add	x23,x23,x7			// h+=X[i]
	eor	x16,x16,x20,ror#18
	eor	x14,x14,x9,ror#8
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x24,x25			// a^b, b^c in next round
	eor	x16,x16,x20,ror#41	// Sigma1(e)
	eor	x15,x15,x24,ror#34
	add	x23,x23,x17			// h+=Ch(e,f,g)
	and	x28,x28,x19			// (b^c)&=(a^b)
	eor	x13,x13,x6,ror#61
	eor	x14,x14,x9,lsr#7	// sigma0(X[i+1])
	add	x23,x23,x16			// h+=Sigma1(e)
	eor	x28,x28,x25			// Maj(a,b,c)
	eor	x17,x15,x24,ror#39	// Sigma0(a)
	eor	x13,x13,x6,lsr#6	// sigma1(X[i+14])
	add	x8,x8,x1
	add	x27,x27,x23			// d+=h
	add	x23,x23,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	add	x8,x8,x14
	add	x23,x23,x17			// h+=Sigma0(a)
	add	x8,x8,x13
	ldr	x13,[sp,#16]
	str	x0,[sp,#8]
	ror	x16,x27,#14
	add	x22,x22,x28			// h+=K[i]
	ror	x15,x10,#1
	and	x17,x20,x27
	ror	x14,x7,#19
	bic	x28,x21,x27
	ror	x0,x23,#28
	add	x22,x22,x8			// h+=X[i]
	eor	x16,x16,x27,ror#18
	eor	x15,x15,x10,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x23,x24			// a^b, b^c in next round
	eor	x16,x16,x27,ror#41	// Sigma1(e)
	eor	x0,x0,x23,ror#34
	add	x22,x22,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x14,x14,x7,ror#61
	eor	x15,x15,x10,lsr#7	// sigma0(X[i+1])
	add	x22,x22,x16			// h+=Sigma1(e)
	eor	x19,x19,x24			// Maj(a,b,c)
	eor	x17,x0,x23,ror#39	// Sigma0(a)
	eor	x14,x14,x7,lsr#6	// sigma1(X[i+14])
	add	x9,x9,x2
	add	x26,x26,x22			// d+=h
	add	x22,x22,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x9,x9,x15
	add	x22,x22,x17			// h+=Sigma0(a)
	add	x9,x9,x14
	ldr	x14,[sp,#24]
	str	x1,[sp,#16]
	ror	x16,x26,#14
	add	x21,x21,x19			// h+=K[i]
	ror	x0,x11,#1
	and	x17,x27,x26
	ror	x15,x8,#19
	bic	x19,x20,x26
	ror	x1,x22,#28
	add	x21,x21,x9			// h+=X[i]
	eor	x16,x16,x26,ror#18
	eor	x0,x0,x11,ror#8
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x22,x23			// a^b, b^c in next round
	eor	x16,x16,x26,ror#41	// Sigma1(e)
	eor	x1,x1,x22,ror#34
	add	x21,x21,x17			// h+=Ch(e,f,g)
	and	x28,x28,x19			// (b^c)&=(a^b)
	eor	x15,x15,x8,ror#61
	eor	x0,x0,x11,lsr#7	// sigma0(X[i+1])
	add	x21,x21,x16			// h+=Sigma1(e)
	eor	x28,x28,x23			// Maj(a,b,c)
	eor	x17,x1,x22,ror#39	// Sigma0(a)
	eor	x15,x15,x8,lsr#6	// sigma1(X[i+14])
	add	x10,x10,x3
	add	x25,x25,x21			// d+=h
	add	x21,x21,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	add	x10,x10,x0
	add	x21,x21,x17			// h+=Sigma0(a)
	add	x10,x10,x15
	ldr	x15,[sp,#0]
	str	x2,[sp,#24]
	ror	x16,x25,#14
	add	x20,x20,x28			// h+=K[i]
	ror	x1,x12,#1
	and	x17,x26,x25
	ror	x0,x9,#19
	bic	x28,x27,x25
	ror	x2,x21,#28
	add	x20,x20,x10			// h+=X[i]
	eor	x16,x16,x25,ror#18
	eor	x1,x1,x12,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x21,x22			// a^b, b^c in next round
	eor	x16,x16,x25,ror#41	// Sigma1(e)
	eor	x2,x2,x21,ror#34
	add	x20,x20,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x0,x0,x9,ror#61
	eor	x1,x1,x12,lsr#7	// sigma0(X[i+1])
	add	x20,x20,x16			// h+=Sigma1(e)
	eor	x19,x19,x22			// Maj(a,b,c)
	eor	x17,x2,x21,ror#39	// Sigma0(a)
	eor	x0,x0,x9,lsr#6	// sigma1(X[i+14])
	add	x11,x11,x4
	add	x24,x24,x20			// d+=h
	add	x20,x20,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x11,x11,x1
	add	x20,x20,x17			// h+=Sigma0(a)
	add	x11,x11,x0
	ldr	x0,[sp,#8]
	str	x3,[sp,#0]
	ror	x16,x24,#14
	add	x27,x27,x19			// h+=K[i]
	ror	x2,x13,#1
	and	x17,x25,x24
	ror	x1,x10,#19
	bic	x19,x26,x24
	ror	x3,x20,#28
	add	x27,x27,x11			// h+=X[i]
	eor	x16,x16,x24,ror#18
	eor	x2,x2,x13,ror#8
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x20,x21			// a^b, b^c in next round
	eor	x16,x16,x24,ror#41	// Sigma1(e)
	eor	x3,x3,x20,ror#34
	add	x27,x27,x17			// h+=Ch(e,f,g)
	and	x28,x28,x19			// (b^c)&=(a^b)
	eor	x1,x1,x10,ror#61
	eor	x2,x2,x13,lsr#7	// sigma0(X[i+1])
	add	x27,x27,x16			// h+=Sigma1(e)
	eor	x28,x28,x21			// Maj(a,b,c)
	eor	x17,x3,x20,ror#39	// Sigma0(a)
	eor	x1,x1,x10,lsr#6	// sigma1(X[i+14])
	add	x12,x12,x5
	add	x23,x23,x27			// d+=h
	add	x27,x27,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	add	x12,x12,x2
	add	x27,x27,x17			// h+=Sigma0(a)
	add	x12,x12,x1
	ldr	x1,[sp,#16]
	str	x4,[sp,#8]
	ror	x16,x23,#14
	add	x26,x26,x28			// h+=K[i]
	ror	x3,x14,#1
	and	x17,x24,x23
	ror	x2,x11,#19
	bic	x28,x25,x23
	ror	x4,x27,#28
	add	x26,x26,x12			// h+=X[i]
	eor	x16,x16,x23,ror#18
	eor	x3,x3,x14,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x27,x20			// a^b, b^c in next round
	eor	x16,x16,x23,ror#41	// Sigma1(e)
	eor	x4,x4,x27,ror#34
	add	x26,x26,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x2,x2,x11,ror#61
	eor	x3,x3,x14,lsr#7	// sigma0(X[i+1])
	add	x26,x26,x16			// h+=Sigma1(e)
	eor	x19,x19,x20			// Maj(a,b,c)
	eor	x17,x4,x27,ror#39	// Sigma0(a)
	eor	x2,x2,x11,lsr#6	// sigma1(X[i+14])
	add	x13,x13,x6
	add	x22,x22,x26			// d+=h
	add	x26,x26,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x13,x13,x3
	add	x26,x26,x17			// h+=Sigma0(a)
	add	x13,x13,x2
	ldr	x2,[sp,#24]
	str	x5,[sp,#16]
	ror	x16,x22,#14
	add	x25,x25,x19			// h+=K[i]
	ror	x4,x15,#1
	and	x17,x23,x22
	ror	x3,x12,#19
	bic	x19,x24,x22
	ror	x5,x26,#28
	add	x25,x25,x13			// h+=X[i]
	eor	x16,x16,x22,ror#18
	eor	x4,x4,x15,ror#8
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x26,x27			// a^b, b^c in next round
	eor	x16,x16,x22,ror#41	// Sigma1(e)
	eor	x5,x5,x26,ror#34
	add	x25,x25,x17			// h+=Ch(e,f,g)
	and	x28,x28,x19			// (b^c)&=(a^b)
	eor	x3,x3,x12,ror#61
	eor	x4,x4,x15,lsr#7	// sigma0(X[i+1])
	add	x25,x25,x16			// h+=Sigma1(e)
	eor	x28,x28,x27			// Maj(a,b,c)
	eor	x17,x5,x26,ror#39	// Sigma0(a)
	eor	x3,x3,x12,lsr#6	// sigma1(X[i+14])
	add	x14,x14,x7
	add	x21,x21,x25			// d+=h
	add	x25,x25,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	add	x14,x14,x4
	add	x25,x25,x17			// h+=Sigma0(a)
	add	x14,x14,x3
	ldr	x3,[sp,#0]
	str	x6,[sp,#24]
	ror	x16,x21,#14
	add	x24,x24,x28			// h+=K[i]
	ror	x5,x0,#1
	and	x17,x22,x21
	ror	x4,x13,#19
	bic	x28,x23,x21
	ror	x6,x25,#28
	add	x24,x24,x14			// h+=X[i]
	eor	x16,x16,x21,ror#18
	eor	x5,x5,x0,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x25,x26			// a^b, b^c in next round
	eor	x16,x16,x21,ror#41	// Sigma1(e)
	eor	x6,x6,x25,ror#34
	add	x24,x24,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x4,x4,x13,ror#61
	eor	x5,x5,x0,lsr#7	// sigma0(X[i+1])
	add	x24,x24,x16			// h+=Sigma1(e)
	eor	x19,x19,x26			// Maj(a,b,c)
	eor	x17,x6,x25,ror#39	// Sigma0(a)
	eor	x4,x4,x13,lsr#6	// sigma1(X[i+14])
	add	x15,x15,x8
	add	x20,x20,x24			// d+=h
	add	x24,x24,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x15,x15,x5
	add	x24,x24,x17			// h+=Sigma0(a)
	add	x15,x15,x4
	ldr	x4,[sp,#8]
	str	x7,[sp,#0]
	ror	x16,x20,#14
	add	x23,x23,x19			// h+=K[i]
	ror	x6,x1,#1
	and	x17,x21,x20
	ror	x5,x14,#19
	bic	x19,x22,x20
	ror	x7,x24,#28
	add	x23,x23,x15			// h+=X[i]
	eor	x16,x16,x20,ror#18
	eor	x6,x6,x1,ror#8
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x24,x25			// a^b, b^c in next round
	eor	x16,x16,x20,ror#41	// Sigma1(e)
	eor	x7,x7,x24,ror#34
	add	x23,x23,x17			// h+=Ch(e,f,g)
	and	x28,x28,x19			// (b^c)&=(a^b)
	eor	x5,x5,x14,ror#61
	eor	x6,x6,x1,lsr#7	// sigma0(X[i+1])
	add	x23,x23,x16			// h+=Sigma1(e)
	eor	x28,x28,x25			// Maj(a,b,c)
	eor	x17,x7,x24,ror#39	// Sigma0(a)
	eor	x5,x5,x14,lsr#6	// sigma1(X[i+14])
	add	x0,x0,x9
	add	x27,x27,x23			// d+=h
	add	x23,x23,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	add	x0,x0,x6
	add	x23,x23,x17			// h+=Sigma0(a)
	add	x0,x0,x5
	ldr	x5,[sp,#16]
	str	x8,[sp,#8]
	ror	x16,x27,#14
	add	x22,x22,x28			// h+=K[i]
	ror	x7,x2,#1
	and	x17,x20,x27
	ror	x6,x15,#19
	bic	x28,x21,x27
	ror	x8,x23,#28
	add	x22,x22,x0			// h+=X[i]
	eor	x16,x16,x27,ror#18
	eor	x7,x7,x2,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x23,x24			// a^b, b^c in next round
	eor	x16,x16,x27,ror#41	// Sigma1(e)
	eor	x8,x8,x23,ror#34
	add	x22,x22,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x6,x6,x15,ror#61
	eor	x7,x7,x2,lsr#7	// sigma0(X[i+1])
	add	x22,x22,x16			// h+=Sigma1(e)
	eor	x19,x19,x24			// Maj(a,b,c)
	eor	x17,x8,x23,ror#39	// Sigma0(a)
	eor	x6,x6,x15,lsr#6	// sigma1(X[i+14])
	add	x1,x1,x10
	add	x26,x26,x22			// d+=h
	add	x22,x22,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x1,x1,x7
	add	x22,x22,x17			// h+=Sigma0(a)
	add	x1,x1,x6
	ldr	x6,[sp,#24]
	str	x9,[sp,#16]
	ror	x16,x26,#14
	add	x21,x21,x19			// h+=K[i]
	ror	x8,x3,#1
	and	x17,x27,x26
	ror	x7,x0,#19
	bic	x19,x20,x26
	ror	x9,x22,#28
	add	x21,x21,x1			// h+=X[i]
	eor	x16,x16,x26,ror#18
	eor	x8,x8,x3,ror#8
	orr	x17,x17,x19			// Ch(e,f,g)
	eor	x19,x22,x23			// a^b, b^c in next round
	eor	x16,x16,x26,ror#41	// Sigma1(e)
	eor	x9,x9,x22,ror#34
	add	x21,x21,x17			// h+=Ch(e,f,g)
	and	x28,x28,x19			// (b^c)&=(a^b)
	eor	x7,x7,x0,ror#61
	eor	x8,x8,x3,lsr#7	// sigma0(X[i+1])
	add	x21,x21,x16			// h+=Sigma1(e)
	eor	x28,x28,x23			// Maj(a,b,c)
	eor	x17,x9,x22,ror#39	// Sigma0(a)
	eor	x7,x7,x0,lsr#6	// sigma1(X[i+14])
	add	x2,x2,x11
	add	x25,x25,x21			// d+=h
	add	x21,x21,x28			// h+=Maj(a,b,c)
	ldr	x28,[x30],#8		// *K++, x19 in next round
	add	x2,x2,x8
	add	x21,x21,x17			// h+=Sigma0(a)
	add	x2,x2,x7
	ldr	x7,[sp,#0]
	str	x10,[sp,#24]
	ror	x16,x25,#14
	add	x20,x20,x28			// h+=K[i]
	ror	x9,x4,#1
	and	x17,x26,x25
	ror	x8,x1,#19
	bic	x28,x27,x25
	ror	x10,x21,#28
	add	x20,x20,x2			// h+=X[i]
	eor	x16,x16,x25,ror#18
	eor	x9,x9,x4,ror#8
	orr	x17,x17,x28			// Ch(e,f,g)
	eor	x28,x21,x22			// a^b, b^c in next round
	eor	x16,x16,x25,ror#41	// Sigma1(e)
	eor	x10,x10,x21,ror#34
	add	x20,x20,x17			// h+=Ch(e,f,g)
	and	x19,x19,x28			// (b^c)&=(a^b)
	eor	x8,x8,x1,ror#61
	eor	x9,x9,x4,lsr#7	// sigma0(X[i+1])
	add	x20,x20,x16			// h+=Sigma1(e)
	eor	x19,x19,x22			// Maj(a,b,c)
	eor	x17,x10,x21,ror#39	// Sigma0(a)
	eor	x8,x8,x1,lsr#6	// sigma1(X[i+14])
	add	x3,x3,x12
	add	x24,x24,x20			// d+=h
	add	x20,x20,x19			// h+=Maj(a,b,c)
	ldr	x19,[x30],#8		// *K++, x28 in next round
	add	x3,x3,x9
	add	x20,x20,x17			// h+=Sigma0(a)
	add	x3,x3,x8
	cbnz	x19,.Loop_16_xx

	ldp	x0,x2,[x29,#96]
	ldr	x1,[x29,#112]
	sub	x30,x30,#648		// rewind

	ldp	x3,x4,[x0]
	ldp	x5,x6,[x0,#2*8]
	add	x1,x1,#14*8			// advance input pointer
	ldp	x7,x8,[x0,#4*8]
	add	x20,x20,x3
	ldp	x9,x10,[x0,#6*8]
	add	x21,x21,x4
	add	x22,x22,x5
	add	x23,x23,x6
	stp	x20,x21,[x0]
	add	x24,x24,x7
	add	x25,x25,x8
	stp	x22,x23,[x0,#2*8]
	add	x26,x26,x9
	add	x27,x27,x10
	cmp	x1,x2
	stp	x24,x25,[x0,#4*8]
	stp	x26,x27,[x0,#6*8]
	b.ne	.Loop

	ldp	x19,x20,[x29,#16]
	add	sp,sp,#4*8
	ldp	x21,x22,[x29,#32]
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldp	x29,x30,[sp],#128
.inst	0xd50323bf				// autiasp
	ret
.size	sha512_block_data_order,.-sha512_block_data_order

.align	6
.type	.LK512,%object
.LK512:
.quad	0x428a2f98d728ae22,0x7137449123ef65cd
.quad	0xb5c0fbcfec4d3b2f,0xe9b5dba58189dbbc
.quad	0x3956c25bf348b538,0x59f111f1b605d019
.quad	0x923f82a4af194f9b,0xab1c5ed5da6d8118
.quad	0xd807aa98a3030242,0x12835b0145706fbe
.quad	0x243185be4ee4b28c,0x550c7dc3d5ffb4e2
.quad	0x72be5d74f27b896f,0x80deb1fe3b1696b1
.quad	0x9bdc06a725c71235,0xc19bf174cf692694
.quad	0xe49b69c19ef14ad2,0xefbe4786384f25e3
.quad	0x0fc19dc68b8cd5b5,0x240ca1cc77ac9c65
.quad	0x2de92c6f592b0275,0x4a7484aa6ea6e483
.quad	0x5cb0a9dcbd41fbd4,0x76f988da831153b5
.quad	0x983e5152ee66dfab,0xa831c66d2db43210
.quad	0xb00327c898fb213f,0xbf597fc7beef0ee4
.quad	0xc6e00bf33da88fc2,0xd5a79147930aa725
.quad	0x06ca6351e003826f,0x142929670a0e6e70
.quad	0x27b70a8546d22ffc,0x2e1b21385c26c926
.quad	0x4d2c6dfc5ac42aed,0x53380d139d95b3df
.quad	0x650a73548baf63de,0x766a0abb3c77b2a8
.quad	0x81c2c92e47edaee6,0x92722c851482353b
.quad	0xa2bfe8a14cf10364,0xa81a664bbc423001
.quad	0xc24b8b70d0f89791,0xc76c51a30654be30
.quad	0xd192e819d6ef5218,0xd69906245565a910
.quad	0xf40e35855771202a,0x106aa07032bbd1b8
.quad	0x19a4c116b8d2d0c8,0x1e376c085141ab53
.quad	0x2748774cdf8eeb99,0x34b0bcb5e19b48a8
.quad	0x391c0cb3c5c95a63,0x4ed8aa4ae3418acb
.quad	0x5b9cca4f7763e373,0x682e6ff3d6b2b8a3
.quad	0x748f82ee5defb2fc,0x78a5636f43172f60
.quad	0x84c87814a1f0ab72,0x8cc702081a6439ec
.quad	0x90befffa23631e28,0xa4506cebde82bde9
.quad	0xbef9a3f7b2c67915,0xc67178f2e372532b
.quad	0xca273eceea26619c,0xd186b8c721c0c207
.quad	0xeada7dd6cde0eb1e,0xf57d4f7fee6ed178
.quad	0x06f067aa72176fba,0x0a637dc5a2c898a6
.quad	0x113f9804bef90dae,0x1b710b35131c471b
.quad	0x28db77f523047d84,0x32caab7b40c72493
.quad	0x3c9ebe0a15c9bebc,0x431d67c49c100d4c
.quad	0x4cc5d4becb3e42b6,0x597f299cfc657e2a
.quad	0x5fcb6fab3ad6faec,0x6c44198c4a475817
.quad	0	// terminator
.size	.LK512,.-.LK512
#ifndef	__KERNEL__
.align	3
.LOPENSSL_armcap_P:
# ifdef	__ILP32__
.long	OPENSSL_armcap_P-.
# else
.quad	OPENSSL_armcap_P-.
# endif
#endif
.byte	83,72,65,53,49,50,32,98,108,111,99,107,32,116,114,97,110,115,102,111,114,109,32,102,111,114,32,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	2
#ifndef	__KERNEL__
.type	sha512_block_armv8,%function
.align	6
sha512_block_armv8:
.Lv8_entry:
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0

	ld1	{v16.16b,v17.16b,v18.16b,v19.16b},[x1],#64	// load input
	ld1	{v20.16b,v21.16b,v22.16b,v23.16b},[x1],#64

	ld1	{v0.2d,v1.2d,v2.2d,v3.2d},[x0]		// load context
	adr	x3,.LK512

	rev64	v16.16b,v16.16b
	rev64	v17.16b,v17.16b
	rev64	v18.16b,v18.16b
	rev64	v19.16b,v19.16b
	rev64	v20.16b,v20.16b
	rev64	v21.16b,v21.16b
	rev64	v22.16b,v22.16b
	rev64	v23.16b,v23.16b
	b	.Loop_hw

.align	4
.Loop_hw:
	ld1	{v24.2d},[x3],#16
	subs	x2,x2,#1
	sub	x4,x1,#128
	orr	v26.16b,v0.16b,v0.16b			// offload
	orr	v27.16b,v1.16b,v1.16b
	orr	v28.16b,v2.16b,v2.16b
	orr	v29.16b,v3.16b,v3.16b
	csel	x1,x1,x4,ne			// conditional rewind
	add	v24.2d,v24.2d,v16.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v2.16b,v3.16b,#8
	ext	v6.16b,v1.16b,v2.16b,#8
	add	v3.2d,v3.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec08230	//sha512su0 v16.16b,v17.16b
	ext	v7.16b,v20.16b,v21.16b,#8
.inst	0xce6680a3	//sha512h v3.16b,v5.16b,v6.16b
.inst	0xce678af0	//sha512su1 v16.16b,v23.16b,v7.16b
	add	v4.2d,v1.2d,v3.2d		// "D + T1"
.inst	0xce608423	//sha512h2 v3.16b,v1.16b,v0.16b
	add	v25.2d,v25.2d,v17.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v4.16b,v2.16b,#8
	ext	v6.16b,v0.16b,v4.16b,#8
	add	v2.2d,v2.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08251	//sha512su0 v17.16b,v18.16b
	ext	v7.16b,v21.16b,v22.16b,#8
.inst	0xce6680a2	//sha512h v2.16b,v5.16b,v6.16b
.inst	0xce678a11	//sha512su1 v17.16b,v16.16b,v7.16b
	add	v1.2d,v0.2d,v2.2d		// "D + T1"
.inst	0xce638402	//sha512h2 v2.16b,v0.16b,v3.16b
	add	v24.2d,v24.2d,v18.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v1.16b,v4.16b,#8
	ext	v6.16b,v3.16b,v1.16b,#8
	add	v4.2d,v4.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec08272	//sha512su0 v18.16b,v19.16b
	ext	v7.16b,v22.16b,v23.16b,#8
.inst	0xce6680a4	//sha512h v4.16b,v5.16b,v6.16b
.inst	0xce678a32	//sha512su1 v18.16b,v17.16b,v7.16b
	add	v0.2d,v3.2d,v4.2d		// "D + T1"
.inst	0xce628464	//sha512h2 v4.16b,v3.16b,v2.16b
	add	v25.2d,v25.2d,v19.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v0.16b,v1.16b,#8
	ext	v6.16b,v2.16b,v0.16b,#8
	add	v1.2d,v1.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08293	//sha512su0 v19.16b,v20.16b
	ext	v7.16b,v23.16b,v16.16b,#8
.inst	0xce6680a1	//sha512h v1.16b,v5.16b,v6.16b
.inst	0xce678a53	//sha512su1 v19.16b,v18.16b,v7.16b
	add	v3.2d,v2.2d,v1.2d		// "D + T1"
.inst	0xce648441	//sha512h2 v1.16b,v2.16b,v4.16b
	add	v24.2d,v24.2d,v20.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v3.16b,v0.16b,#8
	ext	v6.16b,v4.16b,v3.16b,#8
	add	v0.2d,v0.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec082b4	//sha512su0 v20.16b,v21.16b
	ext	v7.16b,v16.16b,v17.16b,#8
.inst	0xce6680a0	//sha512h v0.16b,v5.16b,v6.16b
.inst	0xce678a74	//sha512su1 v20.16b,v19.16b,v7.16b
	add	v2.2d,v4.2d,v0.2d		// "D + T1"
.inst	0xce618480	//sha512h2 v0.16b,v4.16b,v1.16b
	add	v25.2d,v25.2d,v21.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v2.16b,v3.16b,#8
	ext	v6.16b,v1.16b,v2.16b,#8
	add	v3.2d,v3.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec082d5	//sha512su0 v21.16b,v22.16b
	ext	v7.16b,v17.16b,v18.16b,#8
.inst	0xce6680a3	//sha512h v3.16b,v5.16b,v6.16b
.inst	0xce678a95	//sha512su1 v21.16b,v20.16b,v7.16b
	add	v4.2d,v1.2d,v3.2d		// "D + T1"
.inst	0xce608423	//sha512h2 v3.16b,v1.16b,v0.16b
	add	v24.2d,v24.2d,v22.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v4.16b,v2.16b,#8
	ext	v6.16b,v0.16b,v4.16b,#8
	add	v2.2d,v2.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec082f6	//sha512su0 v22.16b,v23.16b
	ext	v7.16b,v18.16b,v19.16b,#8
.inst	0xce6680a2	//sha512h v2.16b,v5.16b,v6.16b
.inst	0xce678ab6	//sha512su1 v22.16b,v21.16b,v7.16b
	add	v1.2d,v0.2d,v2.2d		// "D + T1"
.inst	0xce638402	//sha512h2 v2.16b,v0.16b,v3.16b
	add	v25.2d,v25.2d,v23.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v1.16b,v4.16b,#8
	ext	v6.16b,v3.16b,v1.16b,#8
	add	v4.2d,v4.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08217	//sha512su0 v23.16b,v16.16b
	ext	v7.16b,v19.16b,v20.16b,#8
.inst	0xce6680a4	//sha512h v4.16b,v5.16b,v6.16b
.inst	0xce678ad7	//sha512su1 v23.16b,v22.16b,v7.16b
	add	v0.2d,v3.2d,v4.2d		// "D + T1"
.inst	0xce628464	//sha512h2 v4.16b,v3.16b,v2.16b
	add	v24.2d,v24.2d,v16.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v0.16b,v1.16b,#8
	ext	v6.16b,v2.16b,v0.16b,#8
	add	v1.2d,v1.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec08230	//sha512su0 v16.16b,v17.16b
	ext	v7.16b,v20.16b,v21.16b,#8
.inst	0xce6680a1	//sha512h v1.16b,v5.16b,v6.16b
.inst	0xce678af0	//sha512su1 v16.16b,v23.16b,v7.16b
	add	v3.2d,v2.2d,v1.2d		// "D + T1"
.inst	0xce648441	//sha512h2 v1.16b,v2.16b,v4.16b
	add	v25.2d,v25.2d,v17.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v3.16b,v0.16b,#8
	ext	v6.16b,v4.16b,v3.16b,#8
	add	v0.2d,v0.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08251	//sha512su0 v17.16b,v18.16b
	ext	v7.16b,v21.16b,v22.16b,#8
.inst	0xce6680a0	//sha512h v0.16b,v5.16b,v6.16b
.inst	0xce678a11	//sha512su1 v17.16b,v16.16b,v7.16b
	add	v2.2d,v4.2d,v0.2d		// "D + T1"
.inst	0xce618480	//sha512h2 v0.16b,v4.16b,v1.16b
	add	v24.2d,v24.2d,v18.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v2.16b,v3.16b,#8
	ext	v6.16b,v1.16b,v2.16b,#8
	add	v3.2d,v3.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec08272	//sha512su0 v18.16b,v19.16b
	ext	v7.16b,v22.16b,v23.16b,#8
.inst	0xce6680a3	//sha512h v3.16b,v5.16b,v6.16b
.inst	0xce678a32	//sha512su1 v18.16b,v17.16b,v7.16b
	add	v4.2d,v1.2d,v3.2d		// "D + T1"
.inst	0xce608423	//sha512h2 v3.16b,v1.16b,v0.16b
	add	v25.2d,v25.2d,v19.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v4.16b,v2.16b,#8
	ext	v6.16b,v0.16b,v4.16b,#8
	add	v2.2d,v2.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08293	//sha512su0 v19.16b,v20.16b
	ext	v7.16b,v23.16b,v16.16b,#8
.inst	0xce6680a2	//sha512h v2.16b,v5.16b,v6.16b
.inst	0xce678a53	//sha512su1 v19.16b,v18.16b,v7.16b
	add	v1.2d,v0.2d,v2.2d		// "D + T1"
.inst	0xce638402	//sha512h2 v2.16b,v0.16b,v3.16b
	add	v24.2d,v24.2d,v20.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v1.16b,v4.16b,#8
	ext	v6.16b,v3.16b,v1.16b,#8
	add	v4.2d,v4.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec082b4	//sha512su0 v20.16b,v21.16b
	ext	v7.16b,v16.16b,v17.16b,#8
.inst	0xce6680a4	//sha512h v4.16b,v5.16b,v6.16b
.inst	0xce678a74	//sha512su1 v20.16b,v19.16b,v7.16b
	add	v0.2d,v3.2d,v4.2d		// "D + T1"
.inst	0xce628464	//sha512h2 v4.16b,v3.16b,v2.16b
	add	v25.2d,v25.2d,v21.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v0.16b,v1.16b,#8
	ext	v6.16b,v2.16b,v0.16b,#8
	add	v1.2d,v1.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec082d5	//sha512su0 v21.16b,v22.16b
	ext	v7.16b,v17.16b,v18.16b,#8
.inst	0xce6680a1	//sha512h v1.16b,v5.16b,v6.16b
.inst	0xce678a95	//sha512su1 v21.16b,v20.16b,v7.16b
	add	v3.2d,v2.2d,v1.2d		// "D + T1"
.inst	0xce648441	//sha512h2 v1.16b,v2.16b,v4.16b
	add	v24.2d,v24.2d,v22.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v3.16b,v0.16b,#8
	ext	v6.16b,v4.16b,v3.16b,#8
	add	v0.2d,v0.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec082f6	//sha512su0 v22.16b,v23.16b
	ext	v7.16b,v18.16b,v19.16b,#8
.inst	0xce6680a0	//sha512h v0.16b,v5.16b,v6.16b
.inst	0xce678ab6	//sha512su1 v22.16b,v21.16b,v7.16b
	add	v2.2d,v4.2d,v0.2d		// "D + T1"
.inst	0xce618480	//sha512h2 v0.16b,v4.16b,v1.16b
	add	v25.2d,v25.2d,v23.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v2.16b,v3.16b,#8
	ext	v6.16b,v1.16b,v2.16b,#8
	add	v3.2d,v3.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08217	//sha512su0 v23.16b,v16.16b
	ext	v7.16b,v19.16b,v20.16b,#8
.inst	0xce6680a3	//sha512h v3.16b,v5.16b,v6.16b
.inst	0xce678ad7	//sha512su1 v23.16b,v22.16b,v7.16b
	add	v4.2d,v1.2d,v3.2d		// "D + T1"
.inst	0xce608423	//sha512h2 v3.16b,v1.16b,v0.16b
	add	v24.2d,v24.2d,v16.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v4.16b,v2.16b,#8
	ext	v6.16b,v0.16b,v4.16b,#8
	add	v2.2d,v2.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec08230	//sha512su0 v16.16b,v17.16b
	ext	v7.16b,v20.16b,v21.16b,#8
.inst	0xce6680a2	//sha512h v2.16b,v5.16b,v6.16b
.inst	0xce678af0	//sha512su1 v16.16b,v23.16b,v7.16b
	add	v1.2d,v0.2d,v2.2d		// "D + T1"
.inst	0xce638402	//sha512h2 v2.16b,v0.16b,v3.16b
	add	v25.2d,v25.2d,v17.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v1.16b,v4.16b,#8
	ext	v6.16b,v3.16b,v1.16b,#8
	add	v4.2d,v4.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08251	//sha512su0 v17.16b,v18.16b
	ext	v7.16b,v21.16b,v22.16b,#8
.inst	0xce6680a4	//sha512h v4.16b,v5.16b,v6.16b
.inst	0xce678a11	//sha512su1 v17.16b,v16.16b,v7.16b
	add	v0.2d,v3.2d,v4.2d		// "D + T1"
.inst	0xce628464	//sha512h2 v4.16b,v3.16b,v2.16b
	add	v24.2d,v24.2d,v18.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v0.16b,v1.16b,#8
	ext	v6.16b,v2.16b,v0.16b,#8
	add	v1.2d,v1.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec08272	//sha512su0 v18.16b,v19.16b
	ext	v7.16b,v22.16b,v23.16b,#8
.inst	0xce6680a1	//sha512h v1.16b,v5.16b,v6.16b
.inst	0xce678a32	//sha512su1 v18.16b,v17.16b,v7.16b
	add	v3.2d,v2.2d,v1.2d		// "D + T1"
.inst	0xce648441	//sha512h2 v1.16b,v2.16b,v4.16b
	add	v25.2d,v25.2d,v19.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v3.16b,v0.16b,#8
	ext	v6.16b,v4.16b,v3.16b,#8
	add	v0.2d,v0.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08293	//sha512su0 v19.16b,v20.16b
	ext	v7.16b,v23.16b,v16.16b,#8
.inst	0xce6680a0	//sha512h v0.16b,v5.16b,v6.16b
.inst	0xce678a53	//sha512su1 v19.16b,v18.16b,v7.16b
	add	v2.2d,v4.2d,v0.2d		// "D + T1"
.inst	0xce618480	//sha512h2 v0.16b,v4.16b,v1.16b
	add	v24.2d,v24.2d,v20.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v2.16b,v3.16b,#8
	ext	v6.16b,v1.16b,v2.16b,#8
	add	v3.2d,v3.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec082b4	//sha512su0 v20.16b,v21.16b
	ext	v7.16b,v16.16b,v17.16b,#8
.inst	0xce6680a3	//sha512h v3.16b,v5.16b,v6.16b
.inst	0xce678a74	//sha512su1 v20.16b,v19.16b,v7.16b
	add	v4.2d,v1.2d,v3.2d		// "D + T1"
.inst	0xce608423	//sha512h2 v3.16b,v1.16b,v0.16b
	add	v25.2d,v25.2d,v21.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v4.16b,v2.16b,#8
	ext	v6.16b,v0.16b,v4.16b,#8
	add	v2.2d,v2.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec082d5	//sha512su0 v21.16b,v22.16b
	ext	v7.16b,v17.16b,v18.16b,#8
.inst	0xce6680a2	//sha512h v2.16b,v5.16b,v6.16b
.inst	0xce678a95	//sha512su1 v21.16b,v20.16b,v7.16b
	add	v1.2d,v0.2d,v2.2d		// "D + T1"
.inst	0xce638402	//sha512h2 v2.16b,v0.16b,v3.16b
	add	v24.2d,v24.2d,v22.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v1.16b,v4.16b,#8
	ext	v6.16b,v3.16b,v1.16b,#8
	add	v4.2d,v4.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec082f6	//sha512su0 v22.16b,v23.16b
	ext	v7.16b,v18.16b,v19.16b,#8
.inst	0xce6680a4	//sha512h v4.16b,v5.16b,v6.16b
.inst	0xce678ab6	//sha512su1 v22.16b,v21.16b,v7.16b
	add	v0.2d,v3.2d,v4.2d		// "D + T1"
.inst	0xce628464	//sha512h2 v4.16b,v3.16b,v2.16b
	add	v25.2d,v25.2d,v23.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v0.16b,v1.16b,#8
	ext	v6.16b,v2.16b,v0.16b,#8
	add	v1.2d,v1.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08217	//sha512su0 v23.16b,v16.16b
	ext	v7.16b,v19.16b,v20.16b,#8
.inst	0xce6680a1	//sha512h v1.16b,v5.16b,v6.16b
.inst	0xce678ad7	//sha512su1 v23.16b,v22.16b,v7.16b
	add	v3.2d,v2.2d,v1.2d		// "D + T1"
.inst	0xce648441	//sha512h2 v1.16b,v2.16b,v4.16b
	add	v24.2d,v24.2d,v16.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v3.16b,v0.16b,#8
	ext	v6.16b,v4.16b,v3.16b,#8
	add	v0.2d,v0.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec08230	//sha512su0 v16.16b,v17.16b
	ext	v7.16b,v20.16b,v21.16b,#8
.inst	0xce6680a0	//sha512h v0.16b,v5.16b,v6.16b
.inst	0xce678af0	//sha512su1 v16.16b,v23.16b,v7.16b
	add	v2.2d,v4.2d,v0.2d		// "D + T1"
.inst	0xce618480	//sha512h2 v0.16b,v4.16b,v1.16b
	add	v25.2d,v25.2d,v17.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v2.16b,v3.16b,#8
	ext	v6.16b,v1.16b,v2.16b,#8
	add	v3.2d,v3.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08251	//sha512su0 v17.16b,v18.16b
	ext	v7.16b,v21.16b,v22.16b,#8
.inst	0xce6680a3	//sha512h v3.16b,v5.16b,v6.16b
.inst	0xce678a11	//sha512su1 v17.16b,v16.16b,v7.16b
	add	v4.2d,v1.2d,v3.2d		// "D + T1"
.inst	0xce608423	//sha512h2 v3.16b,v1.16b,v0.16b
	add	v24.2d,v24.2d,v18.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v4.16b,v2.16b,#8
	ext	v6.16b,v0.16b,v4.16b,#8
	add	v2.2d,v2.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec08272	//sha512su0 v18.16b,v19.16b
	ext	v7.16b,v22.16b,v23.16b,#8
.inst	0xce6680a2	//sha512h v2.16b,v5.16b,v6.16b
.inst	0xce678a32	//sha512su1 v18.16b,v17.16b,v7.16b
	add	v1.2d,v0.2d,v2.2d		// "D + T1"
.inst	0xce638402	//sha512h2 v2.16b,v0.16b,v3.16b
	add	v25.2d,v25.2d,v19.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v1.16b,v4.16b,#8
	ext	v6.16b,v3.16b,v1.16b,#8
	add	v4.2d,v4.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08293	//sha512su0 v19.16b,v20.16b
	ext	v7.16b,v23.16b,v16.16b,#8
.inst	0xce6680a4	//sha512h v4.16b,v5.16b,v6.16b
.inst	0xce678a53	//sha512su1 v19.16b,v18.16b,v7.16b
	add	v0.2d,v3.2d,v4.2d		// "D + T1"
.inst	0xce628464	//sha512h2 v4.16b,v3.16b,v2.16b
	add	v24.2d,v24.2d,v20.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v0.16b,v1.16b,#8
	ext	v6.16b,v2.16b,v0.16b,#8
	add	v1.2d,v1.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec082b4	//sha512su0 v20.16b,v21.16b
	ext	v7.16b,v16.16b,v17.16b,#8
.inst	0xce6680a1	//sha512h v1.16b,v5.16b,v6.16b
.inst	0xce678a74	//sha512su1 v20.16b,v19.16b,v7.16b
	add	v3.2d,v2.2d,v1.2d		// "D + T1"
.inst	0xce648441	//sha512h2 v1.16b,v2.16b,v4.16b
	add	v25.2d,v25.2d,v21.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v3.16b,v0.16b,#8
	ext	v6.16b,v4.16b,v3.16b,#8
	add	v0.2d,v0.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec082d5	//sha512su0 v21.16b,v22.16b
	ext	v7.16b,v17.16b,v18.16b,#8
.inst	0xce6680a0	//sha512h v0.16b,v5.16b,v6.16b
.inst	0xce678a95	//sha512su1 v21.16b,v20.16b,v7.16b
	add	v2.2d,v4.2d,v0.2d		// "D + T1"
.inst	0xce618480	//sha512h2 v0.16b,v4.16b,v1.16b
	add	v24.2d,v24.2d,v22.2d
	ld1	{v25.2d},[x3],#16
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v2.16b,v3.16b,#8
	ext	v6.16b,v1.16b,v2.16b,#8
	add	v3.2d,v3.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xcec082f6	//sha512su0 v22.16b,v23.16b
	ext	v7.16b,v18.16b,v19.16b,#8
.inst	0xce6680a3	//sha512h v3.16b,v5.16b,v6.16b
.inst	0xce678ab6	//sha512su1 v22.16b,v21.16b,v7.16b
	add	v4.2d,v1.2d,v3.2d		// "D + T1"
.inst	0xce608423	//sha512h2 v3.16b,v1.16b,v0.16b
	add	v25.2d,v25.2d,v23.2d
	ld1	{v24.2d},[x3],#16
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v4.16b,v2.16b,#8
	ext	v6.16b,v0.16b,v4.16b,#8
	add	v2.2d,v2.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xcec08217	//sha512su0 v23.16b,v16.16b
	ext	v7.16b,v19.16b,v20.16b,#8
.inst	0xce6680a2	//sha512h v2.16b,v5.16b,v6.16b
.inst	0xce678ad7	//sha512su1 v23.16b,v22.16b,v7.16b
	add	v1.2d,v0.2d,v2.2d		// "D + T1"
.inst	0xce638402	//sha512h2 v2.16b,v0.16b,v3.16b
	ld1	{v25.2d},[x3],#16
	add	v24.2d,v24.2d,v16.2d
	ld1	{v16.16b},[x1],#16		// load next input
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v1.16b,v4.16b,#8
	ext	v6.16b,v3.16b,v1.16b,#8
	add	v4.2d,v4.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xce6680a4	//sha512h v4.16b,v5.16b,v6.16b
	rev64	v16.16b,v16.16b
	add	v0.2d,v3.2d,v4.2d		// "D + T1"
.inst	0xce628464	//sha512h2 v4.16b,v3.16b,v2.16b
	ld1	{v24.2d},[x3],#16
	add	v25.2d,v25.2d,v17.2d
	ld1	{v17.16b},[x1],#16		// load next input
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v0.16b,v1.16b,#8
	ext	v6.16b,v2.16b,v0.16b,#8
	add	v1.2d,v1.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xce6680a1	//sha512h v1.16b,v5.16b,v6.16b
	rev64	v17.16b,v17.16b
	add	v3.2d,v2.2d,v1.2d		// "D + T1"
.inst	0xce648441	//sha512h2 v1.16b,v2.16b,v4.16b
	ld1	{v25.2d},[x3],#16
	add	v24.2d,v24.2d,v18.2d
	ld1	{v18.16b},[x1],#16		// load next input
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v3.16b,v0.16b,#8
	ext	v6.16b,v4.16b,v3.16b,#8
	add	v0.2d,v0.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xce6680a0	//sha512h v0.16b,v5.16b,v6.16b
	rev64	v18.16b,v18.16b
	add	v2.2d,v4.2d,v0.2d		// "D + T1"
.inst	0xce618480	//sha512h2 v0.16b,v4.16b,v1.16b
	ld1	{v24.2d},[x3],#16
	add	v25.2d,v25.2d,v19.2d
	ld1	{v19.16b},[x1],#16		// load next input
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v2.16b,v3.16b,#8
	ext	v6.16b,v1.16b,v2.16b,#8
	add	v3.2d,v3.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xce6680a3	//sha512h v3.16b,v5.16b,v6.16b
	rev64	v19.16b,v19.16b
	add	v4.2d,v1.2d,v3.2d		// "D + T1"
.inst	0xce608423	//sha512h2 v3.16b,v1.16b,v0.16b
	ld1	{v25.2d},[x3],#16
	add	v24.2d,v24.2d,v20.2d
	ld1	{v20.16b},[x1],#16		// load next input
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v4.16b,v2.16b,#8
	ext	v6.16b,v0.16b,v4.16b,#8
	add	v2.2d,v2.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xce6680a2	//sha512h v2.16b,v5.16b,v6.16b
	rev64	v20.16b,v20.16b
	add	v1.2d,v0.2d,v2.2d		// "D + T1"
.inst	0xce638402	//sha512h2 v2.16b,v0.16b,v3.16b
	ld1	{v24.2d},[x3],#16
	add	v25.2d,v25.2d,v21.2d
	ld1	{v21.16b},[x1],#16		// load next input
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v1.16b,v4.16b,#8
	ext	v6.16b,v3.16b,v1.16b,#8
	add	v4.2d,v4.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xce6680a4	//sha512h v4.16b,v5.16b,v6.16b
	rev64	v21.16b,v21.16b
	add	v0.2d,v3.2d,v4.2d		// "D + T1"
.inst	0xce628464	//sha512h2 v4.16b,v3.16b,v2.16b
	ld1	{v25.2d},[x3],#16
	add	v24.2d,v24.2d,v22.2d
	ld1	{v22.16b},[x1],#16		// load next input
	ext	v24.16b,v24.16b,v24.16b,#8
	ext	v5.16b,v0.16b,v1.16b,#8
	ext	v6.16b,v2.16b,v0.16b,#8
	add	v1.2d,v1.2d,v24.2d			// "T1 + H + K512[i]"
.inst	0xce6680a1	//sha512h v1.16b,v5.16b,v6.16b
	rev64	v22.16b,v22.16b
	add	v3.2d,v2.2d,v1.2d		// "D + T1"
.inst	0xce648441	//sha512h2 v1.16b,v2.16b,v4.16b
	sub	x3,x3,#80*8	// rewind
	add	v25.2d,v25.2d,v23.2d
	ld1	{v23.16b},[x1],#16		// load next input
	ext	v25.16b,v25.16b,v25.16b,#8
	ext	v5.16b,v3.16b,v0.16b,#8
	ext	v6.16b,v4.16b,v3.16b,#8
	add	v0.2d,v0.2d,v25.2d			// "T1 + H + K512[i]"
.inst	0xce6680a0	//sha512h v0.16b,v5.16b,v6.16b
	rev64	v23.16b,v23.16b
	add	v2.2d,v4.2d,v0.2d		// "D + T1"
.inst	0xce618480	//sha512h2 v0.16b,v4.16b,v1.16b
	add	v0.2d,v0.2d,v26.2d			// accumulate
	add	v1.2d,v1.2d,v27.2d
	add	v2.2d,v2.2d,v28.2d
	add	v3.2d,v3.2d,v29.2d

	cbnz	x2,.Loop_hw

	st1	{v0.2d,v1.2d,v2.2d,v3.2d},[x0]		// store context

	ldr	x29,[sp],#16
	ret
.size	sha512_block_armv8,.-sha512_block_armv8
#endif
#ifndef	__KERNEL__
.comm	OPENSSL_armcap_P,4,4
#endif
