/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from bsaes-armv7.pl. */
@ Copyright 2012-2018 The OpenSSL Project Authors. All Rights Reserved.
@
@ Licensed under the OpenSSL license (the "License").  You may not use
@ this file except in compliance with the License.  You can obtain a copy
@ in the file LICENSE in the source distribution or at
@ https://www.openssl.org/source/license.html


@ ====================================================================
@ Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
@ project. The module is, however, dual licensed under OpenSSL and
@ CRYPTOGAMS licenses depending on where you obtain it. For further
@ details see http://www.openssl.org/~appro/cryptogams/.
@
@ Specific modes and adaptation for Linux kernel by Ard Biesheuvel
@ of Linaro. Permission to use under GPL terms is granted.
@ ====================================================================

@ Bit-sliced AES for ARM NEON
@
@ February 2012.
@
@ This implementation is direct adaptation of bsaes-x86_64 module for
@ ARM NEON. Except that this module is endian-neutral [in sense that
@ it can be compiled for either endianness] by courtesy of vld1.8's
@ neutrality. Initial version doesn't implement interface to OpenSSL,
@ only low-level primitives and unsupported entry points, just enough
@ to collect performance results, which for Cortex-A8 core are:
@
@ encrypt	19.5 cycles per byte processed with 128-bit key
@ decrypt	22.1 cycles per byte processed with 128-bit key
@ key conv.	440  cycles per 128-bit key/0.18 of 8x block
@
@ Snapdragon S4 encrypts byte in 17.6 cycles and decrypts in 19.7,
@ which is [much] worse than anticipated (for further details see
@ http://www.openssl.org/~appro/Snapdragon-S4.html).
@
@ Cortex-A15 manages in 14.2/16.1 cycles [when integer-only code
@ manages in 20.0 cycles].
@
@ When comparing to x86_64 results keep in mind that NEON unit is
@ [mostly] single-issue and thus can't [fully] benefit from
@ instruction-level parallelism. And when comparing to aes-armv4
@ results keep in mind key schedule conversion overhead (see
@ bsaes-x86_64.pl for further details)...
@
@						<appro@openssl.org>

@ April-August 2013
@ Add CBC, CTR and XTS subroutines and adapt for kernel use; courtesy of Ard.

#ifndef __KERNEL__
# include "arm_arch.h"

# define VFP_ABI_PUSH	vstmdb	sp!,{d8-d15}
# define VFP_ABI_POP	vldmia	sp!,{d8-d15}
# define VFP_ABI_FRAME	0x40
#else
# define VFP_ABI_PUSH
# define VFP_ABI_POP
# define VFP_ABI_FRAME	0
# define BSAES_ASM_EXTENDED_KEY
# define XTS_CHAIN_TWEAK
# define __ARM_ARCH__ __LINUX_ARM_ARCH__
# define __ARM_MAX_ARCH__ 7
#endif

#ifdef __thumb__
# define adrl adr
#endif

#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.text
.syntax	unified 	@ ARMv7-capable assembler is expected to handle this
#if defined(__thumb2__) && !defined(__APPLE__)
.thumb
#else
.code	32
# undef __thumb2__
#endif

.type	_bsaes_decrypt8,%function
.align	4
_bsaes_decrypt8:
	adr	r6,.
	vldmia	r4!, {q9}		@ round 0 key
#if defined(__thumb2__) || defined(__APPLE__)
	adr	r6,.LM0ISR
#else
	add	r6,r6,#.LM0ISR-_bsaes_decrypt8
#endif

	vldmia	r6!, {q8}		@ .LM0ISR
	veor	q10, q0, q9	@ xor with round0 key
	veor	q11, q1, q9
	vtbl.8	d0, {q10}, d16
	vtbl.8	d1, {q10}, d17
	veor	q12, q2, q9
	vtbl.8	d2, {q11}, d16
	vtbl.8	d3, {q11}, d17
	veor	q13, q3, q9
	vtbl.8	d4, {q12}, d16
	vtbl.8	d5, {q12}, d17
	veor	q14, q4, q9
	vtbl.8	d6, {q13}, d16
	vtbl.8	d7, {q13}, d17
	veor	q15, q5, q9
	vtbl.8	d8, {q14}, d16
	vtbl.8	d9, {q14}, d17
	veor	q10, q6, q9
	vtbl.8	d10, {q15}, d16
	vtbl.8	d11, {q15}, d17
	veor	q11, q7, q9
	vtbl.8	d12, {q10}, d16
	vtbl.8	d13, {q10}, d17
	vtbl.8	d14, {q11}, d16
	vtbl.8	d15, {q11}, d17
	vmov.i8	q8,#0x55			@ compose .LBS0
	vmov.i8	q9,#0x33			@ compose .LBS1
	vshr.u64	q10, q6, #1
	vshr.u64	q11, q4, #1
	veor	q10, q10, q7
	veor	q11, q11, q5
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q7, q7, q10
	vshl.u64	q10, q10, #1
	veor	q5, q5, q11
	vshl.u64	q11, q11, #1
	veor	q6, q6, q10
	veor	q4, q4, q11
	vshr.u64	q10, q2, #1
	vshr.u64	q11, q0, #1
	veor	q10, q10, q3
	veor	q11, q11, q1
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q3, q3, q10
	vshl.u64	q10, q10, #1
	veor	q1, q1, q11
	vshl.u64	q11, q11, #1
	veor	q2, q2, q10
	veor	q0, q0, q11
	vmov.i8	q8,#0x0f			@ compose .LBS2
	vshr.u64	q10, q5, #2
	vshr.u64	q11, q4, #2
	veor	q10, q10, q7
	veor	q11, q11, q6
	vand	q10, q10, q9
	vand	q11, q11, q9
	veor	q7, q7, q10
	vshl.u64	q10, q10, #2
	veor	q6, q6, q11
	vshl.u64	q11, q11, #2
	veor	q5, q5, q10
	veor	q4, q4, q11
	vshr.u64	q10, q1, #2
	vshr.u64	q11, q0, #2
	veor	q10, q10, q3
	veor	q11, q11, q2
	vand	q10, q10, q9
	vand	q11, q11, q9
	veor	q3, q3, q10
	vshl.u64	q10, q10, #2
	veor	q2, q2, q11
	vshl.u64	q11, q11, #2
	veor	q1, q1, q10
	veor	q0, q0, q11
	vshr.u64	q10, q3, #4
	vshr.u64	q11, q2, #4
	veor	q10, q10, q7
	veor	q11, q11, q6
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q7, q7, q10
	vshl.u64	q10, q10, #4
	veor	q6, q6, q11
	vshl.u64	q11, q11, #4
	veor	q3, q3, q10
	veor	q2, q2, q11
	vshr.u64	q10, q1, #4
	vshr.u64	q11, q0, #4
	veor	q10, q10, q5
	veor	q11, q11, q4
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q5, q5, q10
	vshl.u64	q10, q10, #4
	veor	q4, q4, q11
	vshl.u64	q11, q11, #4
	veor	q1, q1, q10
	veor	q0, q0, q11
	sub	r5,r5,#1
	b	.Ldec_sbox
.align	4
.Ldec_loop:
	vldmia	r4!, {q8,q9,q10,q11}
	veor	q8, q8, q0
	veor	q9, q9, q1
	vtbl.8	d0, {q8}, d24
	vtbl.8	d1, {q8}, d25
	vldmia	r4!, {q8}
	veor	q10, q10, q2
	vtbl.8	d2, {q9}, d24
	vtbl.8	d3, {q9}, d25
	vldmia	r4!, {q9}
	veor	q11, q11, q3
	vtbl.8	d4, {q10}, d24
	vtbl.8	d5, {q10}, d25
	vldmia	r4!, {q10}
	vtbl.8	d6, {q11}, d24
	vtbl.8	d7, {q11}, d25
	vldmia	r4!, {q11}
	veor	q8, q8, q4
	veor	q9, q9, q5
	vtbl.8	d8, {q8}, d24
	vtbl.8	d9, {q8}, d25
	veor	q10, q10, q6
	vtbl.8	d10, {q9}, d24
	vtbl.8	d11, {q9}, d25
	veor	q11, q11, q7
	vtbl.8	d12, {q10}, d24
	vtbl.8	d13, {q10}, d25
	vtbl.8	d14, {q11}, d24
	vtbl.8	d15, {q11}, d25
.Ldec_sbox:
	veor	q1, q1, q4
	veor	q3, q3, q4

	veor	q4, q4, q7
	veor	q1, q1, q6
	veor	q2, q2, q7
	veor	q6, q6, q4

	veor	q0, q0, q1
	veor	q2, q2, q5
	veor	q7, q7, q6
	veor	q3, q3, q0
	veor	q5, q5, q0
	veor	q1, q1, q3
	veor	q11, q3, q0
	veor	q10, q7, q4
	veor	q9, q1, q6
	veor	q13, q4, q0
	vmov	q8, q10
	veor	q12, q5, q2

	vorr	q10, q10, q9
	veor	q15, q11, q8
	vand	q14, q11, q12
	vorr	q11, q11, q12
	veor	q12, q12, q9
	vand	q8, q8, q9
	veor	q9, q6, q2
	vand	q15, q15, q12
	vand	q13, q13, q9
	veor	q9, q3, q7
	veor	q12, q1, q5
	veor	q11, q11, q13
	veor	q10, q10, q13
	vand	q13, q9, q12
	vorr	q9, q9, q12
	veor	q11, q11, q15
	veor	q8, q8, q13
	veor	q10, q10, q14
	veor	q9, q9, q15
	veor	q8, q8, q14
	vand	q12, q4, q6
	veor	q9, q9, q14
	vand	q13, q0, q2
	vand	q14, q7, q1
	vorr	q15, q3, q5
	veor	q11, q11, q12
	veor	q9, q9, q14
	veor	q8, q8, q15
	veor	q10, q10, q13

	@ Inv_GF16 	0, 	1, 	2, 	3, s0, s1, s2, s3

	@ new smaller inversion

	vand	q14, q11, q9
	vmov	q12, q8

	veor	q13, q10, q14
	veor	q15, q8, q14
	veor	q14, q8, q14	@ q14=q15

	vbsl	q13, q9, q8
	vbsl	q15, q11, q10
	veor	q11, q11, q10

	vbsl	q12, q13, q14
	vbsl	q8, q14, q13

	vand	q14, q12, q15
	veor	q9, q9, q8

	veor	q14, q14, q11
	veor	q12, q5, q2
	veor	q8, q1, q6
	veor	q10, q15, q14
	vand	q10, q10, q5
	veor	q5, q5, q1
	vand	q11, q1, q15
	vand	q5, q5, q14
	veor	q1, q11, q10
	veor	q5, q5, q11
	veor	q15, q15, q13
	veor	q14, q14, q9
	veor	q11, q15, q14
	veor	q10, q13, q9
	vand	q11, q11, q12
	vand	q10, q10, q2
	veor	q12, q12, q8
	veor	q2, q2, q6
	vand	q8, q8, q15
	vand	q6, q6, q13
	vand	q12, q12, q14
	vand	q2, q2, q9
	veor	q8, q8, q12
	veor	q2, q2, q6
	veor	q12, q12, q11
	veor	q6, q6, q10
	veor	q5, q5, q12
	veor	q2, q2, q12
	veor	q1, q1, q8
	veor	q6, q6, q8

	veor	q12, q3, q0
	veor	q8, q7, q4
	veor	q11, q15, q14
	veor	q10, q13, q9
	vand	q11, q11, q12
	vand	q10, q10, q0
	veor	q12, q12, q8
	veor	q0, q0, q4
	vand	q8, q8, q15
	vand	q4, q4, q13
	vand	q12, q12, q14
	vand	q0, q0, q9
	veor	q8, q8, q12
	veor	q0, q0, q4
	veor	q12, q12, q11
	veor	q4, q4, q10
	veor	q15, q15, q13
	veor	q14, q14, q9
	veor	q10, q15, q14
	vand	q10, q10, q3
	veor	q3, q3, q7
	vand	q11, q7, q15
	vand	q3, q3, q14
	veor	q7, q11, q10
	veor	q3, q3, q11
	veor	q3, q3, q12
	veor	q0, q0, q12
	veor	q7, q7, q8
	veor	q4, q4, q8
	veor	q1, q1, q7
	veor	q6, q6, q5

	veor	q4, q4, q1
	veor	q2, q2, q7
	veor	q5, q5, q7
	veor	q4, q4, q2
	veor	q7, q7, q0
	veor	q4, q4, q5
	veor	q3, q3, q6
	veor	q6, q6, q1
	veor	q3, q3, q4

	veor	q4, q4, q0
	veor	q7, q7, q3
	subs	r5,r5,#1
	bcc	.Ldec_done
	@ multiplication by 0x05-0x00-0x04-0x00
	vext.8	q8, q0, q0, #8
	vext.8	q14, q3, q3, #8
	vext.8	q15, q5, q5, #8
	veor	q8, q8, q0
	vext.8	q9, q1, q1, #8
	veor	q14, q14, q3
	vext.8	q10, q6, q6, #8
	veor	q15, q15, q5
	vext.8	q11, q4, q4, #8
	veor	q9, q9, q1
	vext.8	q12, q2, q2, #8
	veor	q10, q10, q6
	vext.8	q13, q7, q7, #8
	veor	q11, q11, q4
	veor	q12, q12, q2
	veor	q13, q13, q7

	veor	q0, q0, q14
	veor	q1, q1, q14
	veor	q6, q6, q8
	veor	q2, q2, q10
	veor	q4, q4, q9
	veor	q1, q1, q15
	veor	q6, q6, q15
	veor	q2, q2, q14
	veor	q7, q7, q11
	veor	q4, q4, q14
	veor	q3, q3, q12
	veor	q2, q2, q15
	veor	q7, q7, q15
	veor	q5, q5, q13
	vext.8	q8, q0, q0, #12	@ x0 <<< 32
	vext.8	q9, q1, q1, #12
	veor	q0, q0, q8		@ x0 ^ (x0 <<< 32)
	vext.8	q10, q6, q6, #12
	veor	q1, q1, q9
	vext.8	q11, q4, q4, #12
	veor	q6, q6, q10
	vext.8	q12, q2, q2, #12
	veor	q4, q4, q11
	vext.8	q13, q7, q7, #12
	veor	q2, q2, q12
	vext.8	q14, q3, q3, #12
	veor	q7, q7, q13
	vext.8	q15, q5, q5, #12
	veor	q3, q3, q14

	veor	q9, q9, q0
	veor	q5, q5, q15
	vext.8	q0, q0, q0, #8		@ (x0 ^ (x0 <<< 32)) <<< 64)
	veor	q10, q10, q1
	veor	q8, q8, q5
	veor	q9, q9, q5
	vext.8	q1, q1, q1, #8
	veor	q13, q13, q2
	veor	q0, q0, q8
	veor	q14, q14, q7
	veor	q1, q1, q9
	vext.8	q8, q2, q2, #8
	veor	q12, q12, q4
	vext.8	q9, q7, q7, #8
	veor	q15, q15, q3
	vext.8	q2, q4, q4, #8
	veor	q11, q11, q6
	vext.8	q7, q5, q5, #8
	veor	q12, q12, q5
	vext.8	q4, q3, q3, #8
	veor	q11, q11, q5
	vext.8	q3, q6, q6, #8
	veor	q5, q9, q13
	veor	q11, q11, q2
	veor	q7, q7, q15
	veor	q6, q4, q14
	veor	q4, q8, q12
	veor	q2, q3, q10
	vmov	q3, q11
	 @ vmov	q5, q9
	vldmia	r6, {q12}		@ .LISR
	ite	eq				@ Thumb2 thing, sanity check in ARM
	addeq	r6,r6,#0x10
	bne	.Ldec_loop
	vldmia	r6, {q12}		@ .LISRM0
	b	.Ldec_loop
.align	4
.Ldec_done:
	vmov.i8	q8,#0x55			@ compose .LBS0
	vmov.i8	q9,#0x33			@ compose .LBS1
	vshr.u64	q10, q3, #1
	vshr.u64	q11, q2, #1
	veor	q10, q10, q5
	veor	q11, q11, q7
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q5, q5, q10
	vshl.u64	q10, q10, #1
	veor	q7, q7, q11
	vshl.u64	q11, q11, #1
	veor	q3, q3, q10
	veor	q2, q2, q11
	vshr.u64	q10, q6, #1
	vshr.u64	q11, q0, #1
	veor	q10, q10, q4
	veor	q11, q11, q1
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q4, q4, q10
	vshl.u64	q10, q10, #1
	veor	q1, q1, q11
	vshl.u64	q11, q11, #1
	veor	q6, q6, q10
	veor	q0, q0, q11
	vmov.i8	q8,#0x0f			@ compose .LBS2
	vshr.u64	q10, q7, #2
	vshr.u64	q11, q2, #2
	veor	q10, q10, q5
	veor	q11, q11, q3
	vand	q10, q10, q9
	vand	q11, q11, q9
	veor	q5, q5, q10
	vshl.u64	q10, q10, #2
	veor	q3, q3, q11
	vshl.u64	q11, q11, #2
	veor	q7, q7, q10
	veor	q2, q2, q11
	vshr.u64	q10, q1, #2
	vshr.u64	q11, q0, #2
	veor	q10, q10, q4
	veor	q11, q11, q6
	vand	q10, q10, q9
	vand	q11, q11, q9
	veor	q4, q4, q10
	vshl.u64	q10, q10, #2
	veor	q6, q6, q11
	vshl.u64	q11, q11, #2
	veor	q1, q1, q10
	veor	q0, q0, q11
	vshr.u64	q10, q4, #4
	vshr.u64	q11, q6, #4
	veor	q10, q10, q5
	veor	q11, q11, q3
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q5, q5, q10
	vshl.u64	q10, q10, #4
	veor	q3, q3, q11
	vshl.u64	q11, q11, #4
	veor	q4, q4, q10
	veor	q6, q6, q11
	vshr.u64	q10, q1, #4
	vshr.u64	q11, q0, #4
	veor	q10, q10, q7
	veor	q11, q11, q2
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q7, q7, q10
	vshl.u64	q10, q10, #4
	veor	q2, q2, q11
	vshl.u64	q11, q11, #4
	veor	q1, q1, q10
	veor	q0, q0, q11
	vldmia	r4, {q8}			@ last round key
	veor	q6, q6, q8
	veor	q4, q4, q8
	veor	q2, q2, q8
	veor	q7, q7, q8
	veor	q3, q3, q8
	veor	q5, q5, q8
	veor	q0, q0, q8
	veor	q1, q1, q8
	bx	lr
.size	_bsaes_decrypt8,.-_bsaes_decrypt8

.type	_bsaes_const,%object
.align	6
_bsaes_const:
.LM0ISR:@ InvShiftRows constants
.quad	0x0a0e0206070b0f03, 0x0004080c0d010509
.LISR:
.quad	0x0504070602010003, 0x0f0e0d0c080b0a09
.LISRM0:
.quad	0x01040b0e0205080f, 0x0306090c00070a0d
.LM0SR:@ ShiftRows constants
.quad	0x0a0e02060f03070b, 0x0004080c05090d01
.LSR:
.quad	0x0504070600030201, 0x0f0e0d0c0a09080b
.LSRM0:
.quad	0x0304090e00050a0f, 0x01060b0c0207080d
.LM0:
.quad	0x02060a0e03070b0f, 0x0004080c0105090d
.LREVM0SR:
.quad	0x090d01050c000408, 0x03070b0f060a0e02
.byte	66,105,116,45,115,108,105,99,101,100,32,65,69,83,32,102,111,114,32,78,69,79,78,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	6
.size	_bsaes_const,.-_bsaes_const

.type	_bsaes_encrypt8,%function
.align	4
_bsaes_encrypt8:
	adr	r6,.
	vldmia	r4!, {q9}		@ round 0 key
#if defined(__thumb2__) || defined(__APPLE__)
	adr	r6,.LM0SR
#else
	sub	r6,r6,#_bsaes_encrypt8-.LM0SR
#endif

	vldmia	r6!, {q8}		@ .LM0SR
_bsaes_encrypt8_alt:
	veor	q10, q0, q9	@ xor with round0 key
	veor	q11, q1, q9
	vtbl.8	d0, {q10}, d16
	vtbl.8	d1, {q10}, d17
	veor	q12, q2, q9
	vtbl.8	d2, {q11}, d16
	vtbl.8	d3, {q11}, d17
	veor	q13, q3, q9
	vtbl.8	d4, {q12}, d16
	vtbl.8	d5, {q12}, d17
	veor	q14, q4, q9
	vtbl.8	d6, {q13}, d16
	vtbl.8	d7, {q13}, d17
	veor	q15, q5, q9
	vtbl.8	d8, {q14}, d16
	vtbl.8	d9, {q14}, d17
	veor	q10, q6, q9
	vtbl.8	d10, {q15}, d16
	vtbl.8	d11, {q15}, d17
	veor	q11, q7, q9
	vtbl.8	d12, {q10}, d16
	vtbl.8	d13, {q10}, d17
	vtbl.8	d14, {q11}, d16
	vtbl.8	d15, {q11}, d17
_bsaes_encrypt8_bitslice:
	vmov.i8	q8,#0x55			@ compose .LBS0
	vmov.i8	q9,#0x33			@ compose .LBS1
	vshr.u64	q10, q6, #1
	vshr.u64	q11, q4, #1
	veor	q10, q10, q7
	veor	q11, q11, q5
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q7, q7, q10
	vshl.u64	q10, q10, #1
	veor	q5, q5, q11
	vshl.u64	q11, q11, #1
	veor	q6, q6, q10
	veor	q4, q4, q11
	vshr.u64	q10, q2, #1
	vshr.u64	q11, q0, #1
	veor	q10, q10, q3
	veor	q11, q11, q1
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q3, q3, q10
	vshl.u64	q10, q10, #1
	veor	q1, q1, q11
	vshl.u64	q11, q11, #1
	veor	q2, q2, q10
	veor	q0, q0, q11
	vmov.i8	q8,#0x0f			@ compose .LBS2
	vshr.u64	q10, q5, #2
	vshr.u64	q11, q4, #2
	veor	q10, q10, q7
	veor	q11, q11, q6
	vand	q10, q10, q9
	vand	q11, q11, q9
	veor	q7, q7, q10
	vshl.u64	q10, q10, #2
	veor	q6, q6, q11
	vshl.u64	q11, q11, #2
	veor	q5, q5, q10
	veor	q4, q4, q11
	vshr.u64	q10, q1, #2
	vshr.u64	q11, q0, #2
	veor	q10, q10, q3
	veor	q11, q11, q2
	vand	q10, q10, q9
	vand	q11, q11, q9
	veor	q3, q3, q10
	vshl.u64	q10, q10, #2
	veor	q2, q2, q11
	vshl.u64	q11, q11, #2
	veor	q1, q1, q10
	veor	q0, q0, q11
	vshr.u64	q10, q3, #4
	vshr.u64	q11, q2, #4
	veor	q10, q10, q7
	veor	q11, q11, q6
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q7, q7, q10
	vshl.u64	q10, q10, #4
	veor	q6, q6, q11
	vshl.u64	q11, q11, #4
	veor	q3, q3, q10
	veor	q2, q2, q11
	vshr.u64	q10, q1, #4
	vshr.u64	q11, q0, #4
	veor	q10, q10, q5
	veor	q11, q11, q4
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q5, q5, q10
	vshl.u64	q10, q10, #4
	veor	q4, q4, q11
	vshl.u64	q11, q11, #4
	veor	q1, q1, q10
	veor	q0, q0, q11
	sub	r5,r5,#1
	b	.Lenc_sbox
.align	4
.Lenc_loop:
	vldmia	r4!, {q8,q9,q10,q11}
	veor	q8, q8, q0
	veor	q9, q9, q1
	vtbl.8	d0, {q8}, d24
	vtbl.8	d1, {q8}, d25
	vldmia	r4!, {q8}
	veor	q10, q10, q2
	vtbl.8	d2, {q9}, d24
	vtbl.8	d3, {q9}, d25
	vldmia	r4!, {q9}
	veor	q11, q11, q3
	vtbl.8	d4, {q10}, d24
	vtbl.8	d5, {q10}, d25
	vldmia	r4!, {q10}
	vtbl.8	d6, {q11}, d24
	vtbl.8	d7, {q11}, d25
	vldmia	r4!, {q11}
	veor	q8, q8, q4
	veor	q9, q9, q5
	vtbl.8	d8, {q8}, d24
	vtbl.8	d9, {q8}, d25
	veor	q10, q10, q6
	vtbl.8	d10, {q9}, d24
	vtbl.8	d11, {q9}, d25
	veor	q11, q11, q7
	vtbl.8	d12, {q10}, d24
	vtbl.8	d13, {q10}, d25
	vtbl.8	d14, {q11}, d24
	vtbl.8	d15, {q11}, d25
.Lenc_sbox:
	veor	q2, q2, q1
	veor	q5, q5, q6
	veor	q3, q3, q0
	veor	q6, q6, q2
	veor	q5, q5, q0

	veor	q6, q6, q3
	veor	q3, q3, q7
	veor	q7, q7, q5
	veor	q3, q3, q4
	veor	q4, q4, q5

	veor	q2, q2, q7
	veor	q3, q3, q1
	veor	q1, q1, q5
	veor	q11, q7, q4
	veor	q10, q1, q2
	veor	q9, q5, q3
	veor	q13, q2, q4
	vmov	q8, q10
	veor	q12, q6, q0

	vorr	q10, q10, q9
	veor	q15, q11, q8
	vand	q14, q11, q12
	vorr	q11, q11, q12
	veor	q12, q12, q9
	vand	q8, q8, q9
	veor	q9, q3, q0
	vand	q15, q15, q12
	vand	q13, q13, q9
	veor	q9, q7, q1
	veor	q12, q5, q6
	veor	q11, q11, q13
	veor	q10, q10, q13
	vand	q13, q9, q12
	vorr	q9, q9, q12
	veor	q11, q11, q15
	veor	q8, q8, q13
	veor	q10, q10, q14
	veor	q9, q9, q15
	veor	q8, q8, q14
	vand	q12, q2, q3
	veor	q9, q9, q14
	vand	q13, q4, q0
	vand	q14, q1, q5
	vorr	q15, q7, q6
	veor	q11, q11, q12
	veor	q9, q9, q14
	veor	q8, q8, q15
	veor	q10, q10, q13

	@ Inv_GF16 	0, 	1, 	2, 	3, s0, s1, s2, s3

	@ new smaller inversion

	vand	q14, q11, q9
	vmov	q12, q8

	veor	q13, q10, q14
	veor	q15, q8, q14
	veor	q14, q8, q14	@ q14=q15

	vbsl	q13, q9, q8
	vbsl	q15, q11, q10
	veor	q11, q11, q10

	vbsl	q12, q13, q14
	vbsl	q8, q14, q13

	vand	q14, q12, q15
	veor	q9, q9, q8

	veor	q14, q14, q11
	veor	q12, q6, q0
	veor	q8, q5, q3
	veor	q10, q15, q14
	vand	q10, q10, q6
	veor	q6, q6, q5
	vand	q11, q5, q15
	vand	q6, q6, q14
	veor	q5, q11, q10
	veor	q6, q6, q11
	veor	q15, q15, q13
	veor	q14, q14, q9
	veor	q11, q15, q14
	veor	q10, q13, q9
	vand	q11, q11, q12
	vand	q10, q10, q0
	veor	q12, q12, q8
	veor	q0, q0, q3
	vand	q8, q8, q15
	vand	q3, q3, q13
	vand	q12, q12, q14
	vand	q0, q0, q9
	veor	q8, q8, q12
	veor	q0, q0, q3
	veor	q12, q12, q11
	veor	q3, q3, q10
	veor	q6, q6, q12
	veor	q0, q0, q12
	veor	q5, q5, q8
	veor	q3, q3, q8

	veor	q12, q7, q4
	veor	q8, q1, q2
	veor	q11, q15, q14
	veor	q10, q13, q9
	vand	q11, q11, q12
	vand	q10, q10, q4
	veor	q12, q12, q8
	veor	q4, q4, q2
	vand	q8, q8, q15
	vand	q2, q2, q13
	vand	q12, q12, q14
	vand	q4, q4, q9
	veor	q8, q8, q12
	veor	q4, q4, q2
	veor	q12, q12, q11
	veor	q2, q2, q10
	veor	q15, q15, q13
	veor	q14, q14, q9
	veor	q10, q15, q14
	vand	q10, q10, q7
	veor	q7, q7, q1
	vand	q11, q1, q15
	vand	q7, q7, q14
	veor	q1, q11, q10
	veor	q7, q7, q11
	veor	q7, q7, q12
	veor	q4, q4, q12
	veor	q1, q1, q8
	veor	q2, q2, q8
	veor	q7, q7, q0
	veor	q1, q1, q6
	veor	q6, q6, q0
	veor	q4, q4, q7
	veor	q0, q0, q1

	veor	q1, q1, q5
	veor	q5, q5, q2
	veor	q2, q2, q3
	veor	q3, q3, q5
	veor	q4, q4, q5

	veor	q6, q6, q3
	subs	r5,r5,#1
	bcc	.Lenc_done
	vext.8	q8, q0, q0, #12	@ x0 <<< 32
	vext.8	q9, q1, q1, #12
	veor	q0, q0, q8		@ x0 ^ (x0 <<< 32)
	vext.8	q10, q4, q4, #12
	veor	q1, q1, q9
	vext.8	q11, q6, q6, #12
	veor	q4, q4, q10
	vext.8	q12, q3, q3, #12
	veor	q6, q6, q11
	vext.8	q13, q7, q7, #12
	veor	q3, q3, q12
	vext.8	q14, q2, q2, #12
	veor	q7, q7, q13
	vext.8	q15, q5, q5, #12
	veor	q2, q2, q14

	veor	q9, q9, q0
	veor	q5, q5, q15
	vext.8	q0, q0, q0, #8		@ (x0 ^ (x0 <<< 32)) <<< 64)
	veor	q10, q10, q1
	veor	q8, q8, q5
	veor	q9, q9, q5
	vext.8	q1, q1, q1, #8
	veor	q13, q13, q3
	veor	q0, q0, q8
	veor	q14, q14, q7
	veor	q1, q1, q9
	vext.8	q8, q3, q3, #8
	veor	q12, q12, q6
	vext.8	q9, q7, q7, #8
	veor	q15, q15, q2
	vext.8	q3, q6, q6, #8
	veor	q11, q11, q4
	vext.8	q7, q5, q5, #8
	veor	q12, q12, q5
	vext.8	q6, q2, q2, #8
	veor	q11, q11, q5
	vext.8	q2, q4, q4, #8
	veor	q5, q9, q13
	veor	q4, q8, q12
	veor	q3, q3, q11
	veor	q7, q7, q15
	veor	q6, q6, q14
	 @ vmov	q4, q8
	veor	q2, q2, q10
	 @ vmov	q5, q9
	vldmia	r6, {q12}		@ .LSR
	ite	eq				@ Thumb2 thing, samity check in ARM
	addeq	r6,r6,#0x10
	bne	.Lenc_loop
	vldmia	r6, {q12}		@ .LSRM0
	b	.Lenc_loop
.align	4
.Lenc_done:
	vmov.i8	q8,#0x55			@ compose .LBS0
	vmov.i8	q9,#0x33			@ compose .LBS1
	vshr.u64	q10, q2, #1
	vshr.u64	q11, q3, #1
	veor	q10, q10, q5
	veor	q11, q11, q7
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q5, q5, q10
	vshl.u64	q10, q10, #1
	veor	q7, q7, q11
	vshl.u64	q11, q11, #1
	veor	q2, q2, q10
	veor	q3, q3, q11
	vshr.u64	q10, q4, #1
	vshr.u64	q11, q0, #1
	veor	q10, q10, q6
	veor	q11, q11, q1
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q6, q6, q10
	vshl.u64	q10, q10, #1
	veor	q1, q1, q11
	vshl.u64	q11, q11, #1
	veor	q4, q4, q10
	veor	q0, q0, q11
	vmov.i8	q8,#0x0f			@ compose .LBS2
	vshr.u64	q10, q7, #2
	vshr.u64	q11, q3, #2
	veor	q10, q10, q5
	veor	q11, q11, q2
	vand	q10, q10, q9
	vand	q11, q11, q9
	veor	q5, q5, q10
	vshl.u64	q10, q10, #2
	veor	q2, q2, q11
	vshl.u64	q11, q11, #2
	veor	q7, q7, q10
	veor	q3, q3, q11
	vshr.u64	q10, q1, #2
	vshr.u64	q11, q0, #2
	veor	q10, q10, q6
	veor	q11, q11, q4
	vand	q10, q10, q9
	vand	q11, q11, q9
	veor	q6, q6, q10
	vshl.u64	q10, q10, #2
	veor	q4, q4, q11
	vshl.u64	q11, q11, #2
	veor	q1, q1, q10
	veor	q0, q0, q11
	vshr.u64	q10, q6, #4
	vshr.u64	q11, q4, #4
	veor	q10, q10, q5
	veor	q11, q11, q2
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q5, q5, q10
	vshl.u64	q10, q10, #4
	veor	q2, q2, q11
	vshl.u64	q11, q11, #4
	veor	q6, q6, q10
	veor	q4, q4, q11
	vshr.u64	q10, q1, #4
	vshr.u64	q11, q0, #4
	veor	q10, q10, q7
	veor	q11, q11, q3
	vand	q10, q10, q8
	vand	q11, q11, q8
	veor	q7, q7, q10
	vshl.u64	q10, q10, #4
	veor	q3, q3, q11
	vshl.u64	q11, q11, #4
	veor	q1, q1, q10
	veor	q0, q0, q11
	vldmia	r4, {q8}			@ last round key
	veor	q4, q4, q8
	veor	q6, q6, q8
	veor	q3, q3, q8
	veor	q7, q7, q8
	veor	q2, q2, q8
	veor	q5, q5, q8
	veor	q0, q0, q8
	veor	q1, q1, q8
	bx	lr
.size	_bsaes_encrypt8,.-_bsaes_encrypt8
.type	_bsaes_key_convert,%function
.align	4
_bsaes_key_convert:
	adr	r6,.
	vld1.8	{q7},  [r4]!		@ load round 0 key
#if defined(__thumb2__) || defined(__APPLE__)
	adr	r6,.LM0
#else
	sub	r6,r6,#_bsaes_key_convert-.LM0
#endif
	vld1.8	{q15}, [r4]!		@ load round 1 key

	vmov.i8	q8,  #0x01			@ bit masks
	vmov.i8	q9,  #0x02
	vmov.i8	q10, #0x04
	vmov.i8	q11, #0x08
	vmov.i8	q12, #0x10
	vmov.i8	q13, #0x20
	vldmia	r6, {q14}		@ .LM0

#ifdef __ARMEL__
	vrev32.8	q7,  q7
	vrev32.8	q15, q15
#endif
	sub	r5,r5,#1
	vstmia	r12!, {q7}		@ save round 0 key
	b	.Lkey_loop

.align	4
.Lkey_loop:
	vtbl.8	d14,{q15},d28
	vtbl.8	d15,{q15},d29
	vmov.i8	q6,  #0x40
	vmov.i8	q15, #0x80

	vtst.8	q0, q7, q8
	vtst.8	q1, q7, q9
	vtst.8	q2, q7, q10
	vtst.8	q3, q7, q11
	vtst.8	q4, q7, q12
	vtst.8	q5, q7, q13
	vtst.8	q6, q7, q6
	vtst.8	q7, q7, q15
	vld1.8	{q15}, [r4]!		@ load next round key
	vmvn	q0, q0		@ "pnot"
	vmvn	q1, q1
	vmvn	q5, q5
	vmvn	q6, q6
#ifdef __ARMEL__
	vrev32.8	q15, q15
#endif
	subs	r5,r5,#1
	vstmia	r12!,{q0,q1,q2,q3,q4,q5,q6,q7}		@ write bit-sliced round key
	bne	.Lkey_loop

	vmov.i8	q7,#0x63			@ compose .L63
	@ don't save last round key
	bx	lr
.size	_bsaes_key_convert,.-_bsaes_key_convert



.globl	bsaes_cbc_encrypt
.type	bsaes_cbc_encrypt,%function
.align	5
bsaes_cbc_encrypt:
#ifndef	__KERNEL__
	cmp	r2, #128
#ifndef	__thumb__
	blo	AES_cbc_encrypt
#else
	bhs	1f
	b	AES_cbc_encrypt
1:
#endif
#endif

	@ it is up to the caller to make sure we are called with enc == 0

	mov	ip, sp
	stmdb	sp!, {r4,r5,r6,r7,r8,r9,r10, lr}
	VFP_ABI_PUSH
	ldr	r8, [ip]			@ IV is 1st arg on the stack
	mov	r2, r2, lsr#4		@ len in 16 byte blocks
	sub	sp, #0x10			@ scratch space to carry over the IV
	mov	r9, sp				@ save sp

	ldr	r10, [r3, #240]		@ get # of rounds
#ifndef	BSAES_ASM_EXTENDED_KEY
	@ allocate the key schedule on the stack
	sub	r12, sp, r10, lsl#7		@ 128 bytes per inner round key
	add	r12, #96			@ sifze of bit-slices key schedule

	@ populate the key schedule
	mov	r4, r3			@ pass key
	mov	r5, r10			@ pass # of rounds
	mov	sp, r12				@ sp is sp
	bl	_bsaes_key_convert
	vldmia	sp, {q6}
	vstmia	r12,  {q15}		@ save last round key
	veor	q7, q7, q6	@ fix up round 0 key
	vstmia	sp, {q7}
#else
	ldr	r12, [r3, #244]
	eors	r12, #1
	beq	0f

	@ populate the key schedule
	str	r12, [r3, #244]
	mov	r4, r3			@ pass key
	mov	r5, r10			@ pass # of rounds
	add	r12, r3, #248			@ pass key schedule
	bl	_bsaes_key_convert
	add	r4, r3, #248
	vldmia	r4, {q6}
	vstmia	r12, {q15}			@ save last round key
	veor	q7, q7, q6	@ fix up round 0 key
	vstmia	r4, {q7}

.align	2

#endif

	vld1.8	{q15}, [r8]		@ load IV
	b	.Lcbc_dec_loop

.align	4
.Lcbc_dec_loop:
	subs	r2, r2, #0x8
	bmi	.Lcbc_dec_loop_finish

	vld1.8	{q0,q1}, [r0]!	@ load input
	vld1.8	{q2,q3}, [r0]!
#ifndef	BSAES_ASM_EXTENDED_KEY
	mov	r4, sp			@ pass the key
#else
	add	r4, r3, #248
#endif
	vld1.8	{q4,q5}, [r0]!
	mov	r5, r10
	vld1.8	{q6,q7}, [r0]
	sub	r0, r0, #0x60
	vstmia	r9, {q15}			@ put aside IV

	bl	_bsaes_decrypt8

	vldmia	r9, {q14}			@ reload IV
	vld1.8	{q8,q9}, [r0]!	@ reload input
	veor	q0, q0, q14	@ ^= IV
	vld1.8	{q10,q11}, [r0]!
	veor	q1, q1, q8
	veor	q6, q6, q9
	vld1.8	{q12,q13}, [r0]!
	veor	q4, q4, q10
	veor	q2, q2, q11
	vld1.8	{q14,q15}, [r0]!
	veor	q7, q7, q12
	vst1.8	{q0,q1}, [r1]!	@ write output
	veor	q3, q3, q13
	vst1.8	{q6}, [r1]!
	veor	q5, q5, q14
	vst1.8	{q4}, [r1]!
	vst1.8	{q2}, [r1]!
	vst1.8	{q7}, [r1]!
	vst1.8	{q3}, [r1]!
	vst1.8	{q5}, [r1]!

	b	.Lcbc_dec_loop

.Lcbc_dec_loop_finish:
	adds	r2, r2, #8
	beq	.Lcbc_dec_done

	vld1.8	{q0}, [r0]!		@ load input
	cmp	r2, #2
	blo	.Lcbc_dec_one
	vld1.8	{q1}, [r0]!
#ifndef	BSAES_ASM_EXTENDED_KEY
	mov	r4, sp			@ pass the key
#else
	add	r4, r3, #248
#endif
	mov	r5, r10
	vstmia	r9, {q15}			@ put aside IV
	beq	.Lcbc_dec_two
	vld1.8	{q2}, [r0]!
	cmp	r2, #4
	blo	.Lcbc_dec_three
	vld1.8	{q3}, [r0]!
	beq	.Lcbc_dec_four
	vld1.8	{q4}, [r0]!
	cmp	r2, #6
	blo	.Lcbc_dec_five
	vld1.8	{q5}, [r0]!
	beq	.Lcbc_dec_six
	vld1.8	{q6}, [r0]!
	sub	r0, r0, #0x70

	bl	_bsaes_decrypt8

	vldmia	r9, {q14}			@ reload IV
	vld1.8	{q8,q9}, [r0]!	@ reload input
	veor	q0, q0, q14	@ ^= IV
	vld1.8	{q10,q11}, [r0]!
	veor	q1, q1, q8
	veor	q6, q6, q9
	vld1.8	{q12,q13}, [r0]!
	veor	q4, q4, q10
	veor	q2, q2, q11
	vld1.8	{q15}, [r0]!
	veor	q7, q7, q12
	vst1.8	{q0,q1}, [r1]!	@ write output
	veor	q3, q3, q13
	vst1.8	{q6}, [r1]!
	vst1.8	{q4}, [r1]!
	vst1.8	{q2}, [r1]!
	vst1.8	{q7}, [r1]!
	vst1.8	{q3}, [r1]!
	b	.Lcbc_dec_done
.align	4
.Lcbc_dec_six:
	sub	r0, r0, #0x60
	bl	_bsaes_decrypt8
	vldmia	r9,{q14}			@ reload IV
	vld1.8	{q8,q9}, [r0]!	@ reload input
	veor	q0, q0, q14	@ ^= IV
	vld1.8	{q10,q11}, [r0]!
	veor	q1, q1, q8
	veor	q6, q6, q9
	vld1.8	{q12}, [r0]!
	veor	q4, q4, q10
	veor	q2, q2, q11
	vld1.8	{q15}, [r0]!
	veor	q7, q7, q12
	vst1.8	{q0,q1}, [r1]!	@ write output
	vst1.8	{q6}, [r1]!
	vst1.8	{q4}, [r1]!
	vst1.8	{q2}, [r1]!
	vst1.8	{q7}, [r1]!
	b	.Lcbc_dec_done
.align	4
.Lcbc_dec_five:
	sub	r0, r0, #0x50
	bl	_bsaes_decrypt8
	vldmia	r9, {q14}			@ reload IV
	vld1.8	{q8,q9}, [r0]!	@ reload input
	veor	q0, q0, q14	@ ^= IV
	vld1.8	{q10,q11}, [r0]!
	veor	q1, q1, q8
	veor	q6, q6, q9
	vld1.8	{q15}, [r0]!
	veor	q4, q4, q10
	vst1.8	{q0,q1}, [r1]!	@ write output
	veor	q2, q2, q11
	vst1.8	{q6}, [r1]!
	vst1.8	{q4}, [r1]!
	vst1.8	{q2}, [r1]!
	b	.Lcbc_dec_done
.align	4
.Lcbc_dec_four:
	sub	r0, r0, #0x40
	bl	_bsaes_decrypt8
	vldmia	r9, {q14}			@ reload IV
	vld1.8	{q8,q9}, [r0]!	@ reload input
	veor	q0, q0, q14	@ ^= IV
	vld1.8	{q10}, [r0]!
	veor	q1, q1, q8
	veor	q6, q6, q9
	vld1.8	{q15}, [r0]!
	veor	q4, q4, q10
	vst1.8	{q0,q1}, [r1]!	@ write output
	vst1.8	{q6}, [r1]!
	vst1.8	{q4}, [r1]!
	b	.Lcbc_dec_done
.align	4
.Lcbc_dec_three:
	sub	r0, r0, #0x30
	bl	_bsaes_decrypt8
	vldmia	r9, {q14}			@ reload IV
	vld1.8	{q8,q9}, [r0]!	@ reload input
	veor	q0, q0, q14	@ ^= IV
	vld1.8	{q15}, [r0]!
	veor	q1, q1, q8
	veor	q6, q6, q9
	vst1.8	{q0,q1}, [r1]!	@ write output
	vst1.8	{q6}, [r1]!
	b	.Lcbc_dec_done
.align	4
.Lcbc_dec_two:
	sub	r0, r0, #0x20
	bl	_bsaes_decrypt8
	vldmia	r9, {q14}			@ reload IV
	vld1.8	{q8}, [r0]!		@ reload input
	veor	q0, q0, q14	@ ^= IV
	vld1.8	{q15}, [r0]!		@ reload input
	veor	q1, q1, q8
	vst1.8	{q0,q1}, [r1]!	@ write output
	b	.Lcbc_dec_done
.align	4
.Lcbc_dec_one:
	sub	r0, r0, #0x10
	mov	r10, r1			@ save original out pointer
	mov	r1, r9			@ use the iv scratch space as out buffer
	mov	r2, r3
	vmov	q4,q15		@ just in case ensure that IV
	vmov	q5,q0			@ and input are preserved
	bl	AES_decrypt
	vld1.8	{q0}, [r9]		@ load result
	veor	q0, q0, q4	@ ^= IV
	vmov	q15, q5		@ q5 holds input
	vst1.8	{q0}, [r10]		@ write output

.Lcbc_dec_done:
#ifndef	BSAES_ASM_EXTENDED_KEY
	vmov.i32	q0, #0
	vmov.i32	q1, #0
.Lcbc_dec_bzero:@ wipe key schedule [if any]
	vstmia	sp!, {q0,q1}
	cmp	sp, r9
	bne	.Lcbc_dec_bzero
#endif

	mov	sp, r9
	add	sp, #0x10			@ add sp,r9,#0x10 is no good for thumb
	vst1.8	{q15}, [r8]		@ return IV
	VFP_ABI_POP
	ldmia	sp!, {r4,r5,r6,r7,r8,r9,r10, pc}
.size	bsaes_cbc_encrypt,.-bsaes_cbc_encrypt

.globl	bsaes_ctr32_encrypt_blocks
.type	bsaes_ctr32_encrypt_blocks,%function
.align	5
bsaes_ctr32_encrypt_blocks:
	cmp	r2, #8			@ use plain AES for
	blo	.Lctr_enc_short			@ small sizes

	mov	ip, sp
	stmdb	sp!, {r4,r5,r6,r7,r8,r9,r10, lr}
	VFP_ABI_PUSH
	ldr	r8, [ip]			@ ctr is 1st arg on the stack
	sub	sp, sp, #0x10			@ scratch space to carry over the ctr
	mov	r9, sp				@ save sp

	ldr	r10, [r3, #240]		@ get # of rounds
#ifndef	BSAES_ASM_EXTENDED_KEY
	@ allocate the key schedule on the stack
	sub	r12, sp, r10, lsl#7		@ 128 bytes per inner round key
	add	r12, #96			@ size of bit-sliced key schedule

	@ populate the key schedule
	mov	r4, r3			@ pass key
	mov	r5, r10			@ pass # of rounds
	mov	sp, r12				@ sp is sp
	bl	_bsaes_key_convert
	veor	q7,q7,q15	@ fix up last round key
	vstmia	r12, {q7}			@ save last round key

	vld1.8	{q0}, [r8]		@ load counter
#ifdef	__APPLE__
	mov	r8, #:lower16:(.LREVM0SR-.LM0)
	add	r8, r6, r8
#else
	add	r8, r6, #.LREVM0SR-.LM0	@ borrow r8
#endif
	vldmia	sp, {q4}		@ load round0 key
#else
	ldr	r12, [r3, #244]
	eors	r12, #1
	beq	0f

	@ populate the key schedule
	str	r12, [r3, #244]
	mov	r4, r3			@ pass key
	mov	r5, r10			@ pass # of rounds
	add	r12, r3, #248			@ pass key schedule
	bl	_bsaes_key_convert
	veor	q7,q7,q15	@ fix up last round key
	vstmia	r12, {q7}			@ save last round key

.align	2
	add	r12, r3, #248
	vld1.8	{q0}, [r8]		@ load counter
	adrl	r8, .LREVM0SR			@ borrow r8
	vldmia	r12, {q4}			@ load round0 key
	sub	sp, #0x10			@ place for adjusted round0 key
#endif

	vmov.i32	q8,#1		@ compose 1<<96
	veor	q9,q9,q9
	vrev32.8	q0,q0
	vext.8	q8,q9,q8,#4
	vrev32.8	q4,q4
	vadd.u32	q9,q8,q8	@ compose 2<<96
	vstmia	sp, {q4}		@ save adjusted round0 key
	b	.Lctr_enc_loop

.align	4
.Lctr_enc_loop:
	vadd.u32	q10, q8, q9	@ compose 3<<96
	vadd.u32	q1, q0, q8	@ +1
	vadd.u32	q2, q0, q9	@ +2
	vadd.u32	q3, q0, q10	@ +3
	vadd.u32	q4, q1, q10
	vadd.u32	q5, q2, q10
	vadd.u32	q6, q3, q10
	vadd.u32	q7, q4, q10
	vadd.u32	q10, q5, q10	@ next counter

	@ Borrow prologue from _bsaes_encrypt8 to use the opportunity
	@ to flip byte order in 32-bit counter

	vldmia	sp, {q9}		@ load round0 key
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x10		@ pass next round key
#else
	add	r4, r3, #264
#endif
	vldmia	r8, {q8}			@ .LREVM0SR
	mov	r5, r10			@ pass rounds
	vstmia	r9, {q10}			@ save next counter
#ifdef	__APPLE__
	mov	r6, #:lower16:(.LREVM0SR-.LSR)
	sub	r6, r8, r6
#else
	sub	r6, r8, #.LREVM0SR-.LSR	@ pass constants
#endif

	bl	_bsaes_encrypt8_alt

	subs	r2, r2, #8
	blo	.Lctr_enc_loop_done

	vld1.8	{q8,q9}, [r0]!	@ load input
	vld1.8	{q10,q11}, [r0]!
	veor	q0, q8
	veor	q1, q9
	vld1.8	{q12,q13}, [r0]!
	veor	q4, q10
	veor	q6, q11
	vld1.8	{q14,q15}, [r0]!
	veor	q3, q12
	vst1.8	{q0,q1}, [r1]!	@ write output
	veor	q7, q13
	veor	q2, q14
	vst1.8	{q4}, [r1]!
	veor	q5, q15
	vst1.8	{q6}, [r1]!
	vmov.i32	q8, #1			@ compose 1<<96
	vst1.8	{q3}, [r1]!
	veor	q9, q9, q9
	vst1.8	{q7}, [r1]!
	vext.8	q8, q9, q8, #4
	vst1.8	{q2}, [r1]!
	vadd.u32	q9,q8,q8		@ compose 2<<96
	vst1.8	{q5}, [r1]!
	vldmia	r9, {q0}			@ load counter

	bne	.Lctr_enc_loop
	b	.Lctr_enc_done

.align	4
.Lctr_enc_loop_done:
	add	r2, r2, #8
	vld1.8	{q8}, [r0]!	@ load input
	veor	q0, q8
	vst1.8	{q0}, [r1]!	@ write output
	cmp	r2, #2
	blo	.Lctr_enc_done
	vld1.8	{q9}, [r0]!
	veor	q1, q9
	vst1.8	{q1}, [r1]!
	beq	.Lctr_enc_done
	vld1.8	{q10}, [r0]!
	veor	q4, q10
	vst1.8	{q4}, [r1]!
	cmp	r2, #4
	blo	.Lctr_enc_done
	vld1.8	{q11}, [r0]!
	veor	q6, q11
	vst1.8	{q6}, [r1]!
	beq	.Lctr_enc_done
	vld1.8	{q12}, [r0]!
	veor	q3, q12
	vst1.8	{q3}, [r1]!
	cmp	r2, #6
	blo	.Lctr_enc_done
	vld1.8	{q13}, [r0]!
	veor	q7, q13
	vst1.8	{q7}, [r1]!
	beq	.Lctr_enc_done
	vld1.8	{q14}, [r0]
	veor	q2, q14
	vst1.8	{q2}, [r1]!

.Lctr_enc_done:
	vmov.i32	q0, #0
	vmov.i32	q1, #0
#ifndef	BSAES_ASM_EXTENDED_KEY
.Lctr_enc_bzero:@ wipe key schedule [if any]
	vstmia	sp!, {q0,q1}
	cmp	sp, r9
	bne	.Lctr_enc_bzero
#else
	vstmia	sp, {q0,q1}
#endif

	mov	sp, r9
	add	sp, #0x10		@ add sp,r9,#0x10 is no good for thumb
	VFP_ABI_POP
	ldmia	sp!, {r4,r5,r6,r7,r8,r9,r10, pc}	@ return

.align	4
.Lctr_enc_short:
	ldr	ip, [sp]		@ ctr pointer is passed on stack
	stmdb	sp!, {r4,r5,r6,r7,r8, lr}

	mov	r4, r0		@ copy arguments
	mov	r5, r1
	mov	r6, r2
	mov	r7, r3
	ldr	r8, [ip, #12]		@ load counter .LSW
	vld1.8	{q1}, [ip]		@ load whole counter value
#ifdef __ARMEL__
	rev	r8, r8
#endif
	sub	sp, sp, #0x10
	vst1.8	{q1}, [sp]		@ copy counter value
	sub	sp, sp, #0x10

.Lctr_enc_short_loop:
	add	r0, sp, #0x10		@ input counter value
	mov	r1, sp			@ output on the stack
	mov	r2, r7			@ key

	bl	AES_encrypt

	vld1.8	{q0}, [r4]!	@ load input
	vld1.8	{q1}, [sp]		@ load encrypted counter
	add	r8, r8, #1
#ifdef __ARMEL__
	rev	r0, r8
	str	r0, [sp, #0x1c]		@ next counter value
#else
	str	r8, [sp, #0x1c]		@ next counter value
#endif
	veor	q0,q0,q1
	vst1.8	{q0}, [r5]!	@ store output
	subs	r6, r6, #1
	bne	.Lctr_enc_short_loop

	vmov.i32	q0, #0
	vmov.i32	q1, #0
	vstmia	sp!, {q0,q1}

	ldmia	sp!, {r4,r5,r6,r7,r8, pc}
.size	bsaes_ctr32_encrypt_blocks,.-bsaes_ctr32_encrypt_blocks
.globl	bsaes_xts_encrypt
.type	bsaes_xts_encrypt,%function
.align	4
bsaes_xts_encrypt:
	mov	ip, sp
	stmdb	sp!, {r4,r5,r6,r7,r8,r9,r10, lr}		@ 0x20
	VFP_ABI_PUSH
	mov	r6, sp				@ future r3

	mov	r7, r0
	mov	r8, r1
	mov	r9, r2
	mov	r10, r3

	sub	r0, sp, #0x10			@ 0x10
	bic	r0, #0xf			@ align at 16 bytes
	mov	sp, r0

#ifdef	XTS_CHAIN_TWEAK
	ldr	r0, [ip]			@ pointer to input tweak
#else
	@ generate initial tweak
	ldr	r0, [ip, #4]			@ iv[]
	mov	r1, sp
	ldr	r2, [ip, #0]			@ key2
	bl	AES_encrypt
	mov	r0,sp				@ pointer to initial tweak
#endif

	ldr	r1, [r10, #240]		@ get # of rounds
	mov	r3, r6
#ifndef	BSAES_ASM_EXTENDED_KEY
	@ allocate the key schedule on the stack
	sub	r12, sp, r1, lsl#7		@ 128 bytes per inner round key
	@ add	r12, #96			@ size of bit-sliced key schedule
	sub	r12, #48			@ place for tweak[9]

	@ populate the key schedule
	mov	r4, r10			@ pass key
	mov	r5, r1			@ pass # of rounds
	mov	sp, r12
	add	r12, #0x90			@ pass key schedule
	bl	_bsaes_key_convert
	veor	q7, q7, q15	@ fix up last round key
	vstmia	r12, {q7}			@ save last round key
#else
	ldr	r12, [r10, #244]
	eors	r12, #1
	beq	0f

	str	r12, [r10, #244]
	mov	r4, r10			@ pass key
	mov	r5, r1			@ pass # of rounds
	add	r12, r10, #248			@ pass key schedule
	bl	_bsaes_key_convert
	veor	q7, q7, q15	@ fix up last round key
	vstmia	r12, {q7}

.align	2
	sub	sp, #0x90			@ place for tweak[9]
#endif

	vld1.8	{q8}, [r0]			@ initial tweak
	adr	r2, .Lxts_magic

	subs	r9, #0x80
	blo	.Lxts_enc_short
	b	.Lxts_enc_loop

.align	4
.Lxts_enc_loop:
	vldmia	r2, {q5}	@ load XTS magic
	vshr.s64	q6, q8, #63
	mov	r0, sp
	vand	q6, q6, q5
	vadd.u64	q9, q8, q8
	vst1.64	{q8}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q9, #63
	veor	q9, q9, q6
	vand	q7, q7, q5
	vadd.u64	q10, q9, q9
	vst1.64	{q9}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q10, #63
	veor	q10, q10, q7
	vand	q6, q6, q5
	vld1.8	{q0}, [r7]!
	vadd.u64	q11, q10, q10
	vst1.64	{q10}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q11, #63
	veor	q11, q11, q6
	vand	q7, q7, q5
	vld1.8	{q1}, [r7]!
	veor	q0, q0, q8
	vadd.u64	q12, q11, q11
	vst1.64	{q11}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q12, #63
	veor	q12, q12, q7
	vand	q6, q6, q5
	vld1.8	{q2}, [r7]!
	veor	q1, q1, q9
	vadd.u64	q13, q12, q12
	vst1.64	{q12}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q13, #63
	veor	q13, q13, q6
	vand	q7, q7, q5
	vld1.8	{q3}, [r7]!
	veor	q2, q2, q10
	vadd.u64	q14, q13, q13
	vst1.64	{q13}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q14, #63
	veor	q14, q14, q7
	vand	q6, q6, q5
	vld1.8	{q4}, [r7]!
	veor	q3, q3, q11
	vadd.u64	q15, q14, q14
	vst1.64	{q14}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q15, #63
	veor	q15, q15, q6
	vand	q7, q7, q5
	vld1.8	{q5}, [r7]!
	veor	q4, q4, q12
	vadd.u64	q8, q15, q15
	vst1.64	{q15}, [r0,:128]!
	vswp	d15,d14
	veor	q8, q8, q7
	vst1.64	{q8}, [r0,:128]		@ next round tweak

	vld1.8	{q6,q7}, [r7]!
	veor	q5, q5, q13
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q6, q6, q14
	mov	r5, r1			@ pass rounds
	veor	q7, q7, q15
	mov	r0, sp

	bl	_bsaes_encrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	vld1.64	{q12,q13}, [r0,:128]!
	veor	q1, q1, q9
	veor	q8, q4, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q6, q11
	vld1.64	{q14,q15}, [r0,:128]!
	veor	q10, q3, q12
	vst1.8	{q8,q9}, [r8]!
	veor	q11, q7, q13
	veor	q12, q2, q14
	vst1.8	{q10,q11}, [r8]!
	veor	q13, q5, q15
	vst1.8	{q12,q13}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak

	subs	r9, #0x80
	bpl	.Lxts_enc_loop

.Lxts_enc_short:
	adds	r9, #0x70
	bmi	.Lxts_enc_done

	vldmia	r2, {q5}	@ load XTS magic
	vshr.s64	q7, q8, #63
	mov	r0, sp
	vand	q7, q7, q5
	vadd.u64	q9, q8, q8
	vst1.64	{q8}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q9, #63
	veor	q9, q9, q7
	vand	q6, q6, q5
	vadd.u64	q10, q9, q9
	vst1.64	{q9}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q10, #63
	veor	q10, q10, q6
	vand	q7, q7, q5
	vld1.8	{q0}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_enc_1
	vadd.u64	q11, q10, q10
	vst1.64	{q10}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q11, #63
	veor	q11, q11, q7
	vand	q6, q6, q5
	vld1.8	{q1}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_enc_2
	veor	q0, q0, q8
	vadd.u64	q12, q11, q11
	vst1.64	{q11}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q12, #63
	veor	q12, q12, q6
	vand	q7, q7, q5
	vld1.8	{q2}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_enc_3
	veor	q1, q1, q9
	vadd.u64	q13, q12, q12
	vst1.64	{q12}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q13, #63
	veor	q13, q13, q7
	vand	q6, q6, q5
	vld1.8	{q3}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_enc_4
	veor	q2, q2, q10
	vadd.u64	q14, q13, q13
	vst1.64	{q13}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q14, #63
	veor	q14, q14, q6
	vand	q7, q7, q5
	vld1.8	{q4}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_enc_5
	veor	q3, q3, q11
	vadd.u64	q15, q14, q14
	vst1.64	{q14}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q15, #63
	veor	q15, q15, q7
	vand	q6, q6, q5
	vld1.8	{q5}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_enc_6
	veor	q4, q4, q12
	sub	r9, #0x10
	vst1.64	{q15}, [r0,:128]		@ next round tweak

	vld1.8	{q6}, [r7]!
	veor	q5, q5, q13
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q6, q6, q14
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_encrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	vld1.64	{q12,q13}, [r0,:128]!
	veor	q1, q1, q9
	veor	q8, q4, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q6, q11
	vld1.64	{q14}, [r0,:128]!
	veor	q10, q3, q12
	vst1.8	{q8,q9}, [r8]!
	veor	q11, q7, q13
	veor	q12, q2, q14
	vst1.8	{q10,q11}, [r8]!
	vst1.8	{q12}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_enc_done
.align	4
.Lxts_enc_6:
	veor	q4, q4, q12
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q5, q5, q13
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_encrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	vld1.64	{q12,q13}, [r0,:128]!
	veor	q1, q1, q9
	veor	q8, q4, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q6, q11
	veor	q10, q3, q12
	vst1.8	{q8,q9}, [r8]!
	veor	q11, q7, q13
	vst1.8	{q10,q11}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_enc_done

@ put this in range for both ARM and Thumb mode adr instructions
.align	5
.Lxts_magic:
.quad	1, 0x87

.align	5
.Lxts_enc_5:
	veor	q3, q3, q11
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q4, q4, q12
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_encrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	vld1.64	{q12}, [r0,:128]!
	veor	q1, q1, q9
	veor	q8, q4, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q6, q11
	veor	q10, q3, q12
	vst1.8	{q8,q9}, [r8]!
	vst1.8	{q10}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_enc_done
.align	4
.Lxts_enc_4:
	veor	q2, q2, q10
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q3, q3, q11
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_encrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	veor	q1, q1, q9
	veor	q8, q4, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q6, q11
	vst1.8	{q8,q9}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_enc_done
.align	4
.Lxts_enc_3:
	veor	q1, q1, q9
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q2, q2, q10
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_encrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10}, [r0,:128]!
	veor	q0, q0, q8
	veor	q1, q1, q9
	veor	q8, q4, q10
	vst1.8	{q0,q1}, [r8]!
	vst1.8	{q8}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_enc_done
.align	4
.Lxts_enc_2:
	veor	q0, q0, q8
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q1, q1, q9
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_encrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	veor	q0, q0, q8
	veor	q1, q1, q9
	vst1.8	{q0,q1}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_enc_done
.align	4
.Lxts_enc_1:
	mov	r0, sp
	veor	q0, q0, q8
	mov	r1, sp
	vst1.8	{q0}, [sp,:128]
	mov	r2, r10
	mov	r4, r3				@ preserve fp

	bl	AES_encrypt

	vld1.8	{q0}, [sp,:128]
	veor	q0, q0, q8
	vst1.8	{q0}, [r8]!
	mov	r3, r4

	vmov	q8, q9		@ next round tweak

.Lxts_enc_done:
#ifndef	XTS_CHAIN_TWEAK
	adds	r9, #0x10
	beq	.Lxts_enc_ret
	sub	r6, r8, #0x10

.Lxts_enc_steal:
	ldrb	r0, [r7], #1
	ldrb	r1, [r8, #-0x10]
	strb	r0, [r8, #-0x10]
	strb	r1, [r8], #1

	subs	r9, #1
	bhi	.Lxts_enc_steal

	vld1.8	{q0}, [r6]
	mov	r0, sp
	veor	q0, q0, q8
	mov	r1, sp
	vst1.8	{q0}, [sp,:128]
	mov	r2, r10
	mov	r4, r3			@ preserve fp

	bl	AES_encrypt

	vld1.8	{q0}, [sp,:128]
	veor	q0, q0, q8
	vst1.8	{q0}, [r6]
	mov	r3, r4
#endif

.Lxts_enc_ret:
	bic	r0, r3, #0xf
	vmov.i32	q0, #0
	vmov.i32	q1, #0
#ifdef	XTS_CHAIN_TWEAK
	ldr	r1, [r3, #0x20+VFP_ABI_FRAME]	@ chain tweak
#endif
.Lxts_enc_bzero:@ wipe key schedule [if any]
	vstmia	sp!, {q0,q1}
	cmp	sp, r0
	bne	.Lxts_enc_bzero

	mov	sp, r3
#ifdef	XTS_CHAIN_TWEAK
	vst1.8	{q8}, [r1]
#endif
	VFP_ABI_POP
	ldmia	sp!, {r4,r5,r6,r7,r8,r9,r10, pc}	@ return

.size	bsaes_xts_encrypt,.-bsaes_xts_encrypt

.globl	bsaes_xts_decrypt
.type	bsaes_xts_decrypt,%function
.align	4
bsaes_xts_decrypt:
	mov	ip, sp
	stmdb	sp!, {r4,r5,r6,r7,r8,r9,r10, lr}		@ 0x20
	VFP_ABI_PUSH
	mov	r6, sp				@ future r3

	mov	r7, r0
	mov	r8, r1
	mov	r9, r2
	mov	r10, r3

	sub	r0, sp, #0x10			@ 0x10
	bic	r0, #0xf			@ align at 16 bytes
	mov	sp, r0

#ifdef	XTS_CHAIN_TWEAK
	ldr	r0, [ip]			@ pointer to input tweak
#else
	@ generate initial tweak
	ldr	r0, [ip, #4]			@ iv[]
	mov	r1, sp
	ldr	r2, [ip, #0]			@ key2
	bl	AES_encrypt
	mov	r0, sp				@ pointer to initial tweak
#endif

	ldr	r1, [r10, #240]		@ get # of rounds
	mov	r3, r6
#ifndef	BSAES_ASM_EXTENDED_KEY
	@ allocate the key schedule on the stack
	sub	r12, sp, r1, lsl#7		@ 128 bytes per inner round key
	@ add	r12, #96			@ size of bit-sliced key schedule
	sub	r12, #48			@ place for tweak[9]

	@ populate the key schedule
	mov	r4, r10			@ pass key
	mov	r5, r1			@ pass # of rounds
	mov	sp, r12
	add	r12, #0x90			@ pass key schedule
	bl	_bsaes_key_convert
	add	r4, sp, #0x90
	vldmia	r4, {q6}
	vstmia	r12,  {q15}		@ save last round key
	veor	q7, q7, q6	@ fix up round 0 key
	vstmia	r4, {q7}
#else
	ldr	r12, [r10, #244]
	eors	r12, #1
	beq	0f

	str	r12, [r10, #244]
	mov	r4, r10			@ pass key
	mov	r5, r1			@ pass # of rounds
	add	r12, r10, #248			@ pass key schedule
	bl	_bsaes_key_convert
	add	r4, r10, #248
	vldmia	r4, {q6}
	vstmia	r12,  {q15}		@ save last round key
	veor	q7, q7, q6	@ fix up round 0 key
	vstmia	r4, {q7}

.align	2
	sub	sp, #0x90			@ place for tweak[9]
#endif
	vld1.8	{q8}, [r0]			@ initial tweak
	adr	r2, .Lxts_magic

#ifndef	XTS_CHAIN_TWEAK
	tst	r9, #0xf			@ if not multiple of 16
	it	ne				@ Thumb2 thing, sanity check in ARM
	subne	r9, #0x10			@ subtract another 16 bytes
#endif
	subs	r9, #0x80

	blo	.Lxts_dec_short
	b	.Lxts_dec_loop

.align	4
.Lxts_dec_loop:
	vldmia	r2, {q5}	@ load XTS magic
	vshr.s64	q6, q8, #63
	mov	r0, sp
	vand	q6, q6, q5
	vadd.u64	q9, q8, q8
	vst1.64	{q8}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q9, #63
	veor	q9, q9, q6
	vand	q7, q7, q5
	vadd.u64	q10, q9, q9
	vst1.64	{q9}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q10, #63
	veor	q10, q10, q7
	vand	q6, q6, q5
	vld1.8	{q0}, [r7]!
	vadd.u64	q11, q10, q10
	vst1.64	{q10}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q11, #63
	veor	q11, q11, q6
	vand	q7, q7, q5
	vld1.8	{q1}, [r7]!
	veor	q0, q0, q8
	vadd.u64	q12, q11, q11
	vst1.64	{q11}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q12, #63
	veor	q12, q12, q7
	vand	q6, q6, q5
	vld1.8	{q2}, [r7]!
	veor	q1, q1, q9
	vadd.u64	q13, q12, q12
	vst1.64	{q12}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q13, #63
	veor	q13, q13, q6
	vand	q7, q7, q5
	vld1.8	{q3}, [r7]!
	veor	q2, q2, q10
	vadd.u64	q14, q13, q13
	vst1.64	{q13}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q14, #63
	veor	q14, q14, q7
	vand	q6, q6, q5
	vld1.8	{q4}, [r7]!
	veor	q3, q3, q11
	vadd.u64	q15, q14, q14
	vst1.64	{q14}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q15, #63
	veor	q15, q15, q6
	vand	q7, q7, q5
	vld1.8	{q5}, [r7]!
	veor	q4, q4, q12
	vadd.u64	q8, q15, q15
	vst1.64	{q15}, [r0,:128]!
	vswp	d15,d14
	veor	q8, q8, q7
	vst1.64	{q8}, [r0,:128]		@ next round tweak

	vld1.8	{q6,q7}, [r7]!
	veor	q5, q5, q13
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q6, q6, q14
	mov	r5, r1			@ pass rounds
	veor	q7, q7, q15
	mov	r0, sp

	bl	_bsaes_decrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	vld1.64	{q12,q13}, [r0,:128]!
	veor	q1, q1, q9
	veor	q8, q6, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q4, q11
	vld1.64	{q14,q15}, [r0,:128]!
	veor	q10, q2, q12
	vst1.8	{q8,q9}, [r8]!
	veor	q11, q7, q13
	veor	q12, q3, q14
	vst1.8	{q10,q11}, [r8]!
	veor	q13, q5, q15
	vst1.8	{q12,q13}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak

	subs	r9, #0x80
	bpl	.Lxts_dec_loop

.Lxts_dec_short:
	adds	r9, #0x70
	bmi	.Lxts_dec_done

	vldmia	r2, {q5}	@ load XTS magic
	vshr.s64	q7, q8, #63
	mov	r0, sp
	vand	q7, q7, q5
	vadd.u64	q9, q8, q8
	vst1.64	{q8}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q9, #63
	veor	q9, q9, q7
	vand	q6, q6, q5
	vadd.u64	q10, q9, q9
	vst1.64	{q9}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q10, #63
	veor	q10, q10, q6
	vand	q7, q7, q5
	vld1.8	{q0}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_dec_1
	vadd.u64	q11, q10, q10
	vst1.64	{q10}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q11, #63
	veor	q11, q11, q7
	vand	q6, q6, q5
	vld1.8	{q1}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_dec_2
	veor	q0, q0, q8
	vadd.u64	q12, q11, q11
	vst1.64	{q11}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q12, #63
	veor	q12, q12, q6
	vand	q7, q7, q5
	vld1.8	{q2}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_dec_3
	veor	q1, q1, q9
	vadd.u64	q13, q12, q12
	vst1.64	{q12}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q13, #63
	veor	q13, q13, q7
	vand	q6, q6, q5
	vld1.8	{q3}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_dec_4
	veor	q2, q2, q10
	vadd.u64	q14, q13, q13
	vst1.64	{q13}, [r0,:128]!
	vswp	d13,d12
	vshr.s64	q7, q14, #63
	veor	q14, q14, q6
	vand	q7, q7, q5
	vld1.8	{q4}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_dec_5
	veor	q3, q3, q11
	vadd.u64	q15, q14, q14
	vst1.64	{q14}, [r0,:128]!
	vswp	d15,d14
	vshr.s64	q6, q15, #63
	veor	q15, q15, q7
	vand	q6, q6, q5
	vld1.8	{q5}, [r7]!
	subs	r9, #0x10
	bmi	.Lxts_dec_6
	veor	q4, q4, q12
	sub	r9, #0x10
	vst1.64	{q15}, [r0,:128]		@ next round tweak

	vld1.8	{q6}, [r7]!
	veor	q5, q5, q13
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q6, q6, q14
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_decrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	vld1.64	{q12,q13}, [r0,:128]!
	veor	q1, q1, q9
	veor	q8, q6, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q4, q11
	vld1.64	{q14}, [r0,:128]!
	veor	q10, q2, q12
	vst1.8	{q8,q9}, [r8]!
	veor	q11, q7, q13
	veor	q12, q3, q14
	vst1.8	{q10,q11}, [r8]!
	vst1.8	{q12}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_dec_done
.align	4
.Lxts_dec_6:
	vst1.64	{q14}, [r0,:128]		@ next round tweak

	veor	q4, q4, q12
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q5, q5, q13
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_decrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	vld1.64	{q12,q13}, [r0,:128]!
	veor	q1, q1, q9
	veor	q8, q6, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q4, q11
	veor	q10, q2, q12
	vst1.8	{q8,q9}, [r8]!
	veor	q11, q7, q13
	vst1.8	{q10,q11}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_dec_done
.align	4
.Lxts_dec_5:
	veor	q3, q3, q11
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q4, q4, q12
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_decrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	vld1.64	{q12}, [r0,:128]!
	veor	q1, q1, q9
	veor	q8, q6, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q4, q11
	veor	q10, q2, q12
	vst1.8	{q8,q9}, [r8]!
	vst1.8	{q10}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_dec_done
.align	4
.Lxts_dec_4:
	veor	q2, q2, q10
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q3, q3, q11
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_decrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10,q11}, [r0,:128]!
	veor	q0, q0, q8
	veor	q1, q1, q9
	veor	q8, q6, q10
	vst1.8	{q0,q1}, [r8]!
	veor	q9, q4, q11
	vst1.8	{q8,q9}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_dec_done
.align	4
.Lxts_dec_3:
	veor	q1, q1, q9
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q2, q2, q10
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_decrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	vld1.64	{q10}, [r0,:128]!
	veor	q0, q0, q8
	veor	q1, q1, q9
	veor	q8, q6, q10
	vst1.8	{q0,q1}, [r8]!
	vst1.8	{q8}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_dec_done
.align	4
.Lxts_dec_2:
	veor	q0, q0, q8
#ifndef	BSAES_ASM_EXTENDED_KEY
	add	r4, sp, #0x90			@ pass key schedule
#else
	add	r4, r10, #248			@ pass key schedule
#endif
	veor	q1, q1, q9
	mov	r5, r1			@ pass rounds
	mov	r0, sp

	bl	_bsaes_decrypt8

	vld1.64	{q8,q9}, [r0,:128]!
	veor	q0, q0, q8
	veor	q1, q1, q9
	vst1.8	{q0,q1}, [r8]!

	vld1.64	{q8}, [r0,:128]		@ next round tweak
	b	.Lxts_dec_done
.align	4
.Lxts_dec_1:
	mov	r0, sp
	veor	q0, q0, q8
	mov	r1, sp
	vst1.8	{q0}, [sp,:128]
	mov	r5, r2			@ preserve magic
	mov	r2, r10
	mov	r4, r3				@ preserve fp

	bl	AES_decrypt

	vld1.8	{q0}, [sp,:128]
	veor	q0, q0, q8
	vst1.8	{q0}, [r8]!
	mov	r3, r4
	mov	r2, r5

	vmov	q8, q9		@ next round tweak

.Lxts_dec_done:
#ifndef	XTS_CHAIN_TWEAK
	adds	r9, #0x10
	beq	.Lxts_dec_ret

	@ calculate one round of extra tweak for the stolen ciphertext
	vldmia	r2, {q5}
	vshr.s64	q6, q8, #63
	vand	q6, q6, q5
	vadd.u64	q9, q8, q8
	vswp	d13,d12
	veor	q9, q9, q6

	@ perform the final decryption with the last tweak value
	vld1.8	{q0}, [r7]!
	mov	r0, sp
	veor	q0, q0, q9
	mov	r1, sp
	vst1.8	{q0}, [sp,:128]
	mov	r2, r10
	mov	r4, r3			@ preserve fp

	bl	AES_decrypt

	vld1.8	{q0}, [sp,:128]
	veor	q0, q0, q9
	vst1.8	{q0}, [r8]

	mov	r6, r8
.Lxts_dec_steal:
	ldrb	r1, [r8]
	ldrb	r0, [r7], #1
	strb	r1, [r8, #0x10]
	strb	r0, [r8], #1

	subs	r9, #1
	bhi	.Lxts_dec_steal

	vld1.8	{q0}, [r6]
	mov	r0, sp
	veor	q0, q8
	mov	r1, sp
	vst1.8	{q0}, [sp,:128]
	mov	r2, r10

	bl	AES_decrypt

	vld1.8	{q0}, [sp,:128]
	veor	q0, q0, q8
	vst1.8	{q0}, [r6]
	mov	r3, r4
#endif

.Lxts_dec_ret:
	bic	r0, r3, #0xf
	vmov.i32	q0, #0
	vmov.i32	q1, #0
#ifdef	XTS_CHAIN_TWEAK
	ldr	r1, [r3, #0x20+VFP_ABI_FRAME]	@ chain tweak
#endif
.Lxts_dec_bzero:@ wipe key schedule [if any]
	vstmia	sp!, {q0,q1}
	cmp	sp, r0
	bne	.Lxts_dec_bzero

	mov	sp, r3
#ifdef	XTS_CHAIN_TWEAK
	vst1.8	{q8}, [r1]
#endif
	VFP_ABI_POP
	ldmia	sp!, {r4,r5,r6,r7,r8,r9,r10, pc}	@ return

.size	bsaes_xts_decrypt,.-bsaes_xts_decrypt
#endif
