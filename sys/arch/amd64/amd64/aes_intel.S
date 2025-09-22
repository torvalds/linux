/*	$OpenBSD: aes_intel.S,v 1.14 2021/09/04 22:15:33 bluhm Exp $	*/

/*
 * Implement AES algorithm in Intel AES-NI instructions.
 *
 * The white paper of AES-NI instructions can be downloaded from:
 *   http://softwarecommunity.intel.com/isn/downloads/intelavx/AES-Instructions-Set_WP.pdf
 *
 * Copyright (C) 2008-2010, Intel Corporation
 *    Author: Huang Ying <ying.huang@intel.com>
 *            Vinodh Gopal <vinodh.gopal@intel.com>
 *            Kahraman Akdemir
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following
 * conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 * - Neither the name of Intel Corporation nor the names of its
 *   contributors may be used to endorse or promote products
 *   derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Changes to the original source code released by Intel:
 *
 * - assembler macros were converted to the actual instructions;
 * - aesni_ctr_enc was changed to be RFC 3686 compliant;
 * - aes-gcm mode added;
 * - aes-xts implementation added;
 *
 * Copyright (c) 2010,2011 Mike Belopuhov
 * Copyright (c) 2013 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <machine/param.h>
#include <machine/asm.h>

#define STATE1		%xmm0
#define STATE2		%xmm4
#define STATE3		%xmm5
#define STATE4		%xmm6
#define STATE		STATE1
#define IN1		%xmm1
#define IN2		%xmm7
#define IN3		%xmm8
#define IN4		%xmm9
#define IN		IN1
#define KEY		%xmm2
#define IV		%xmm3
#define BSWAP_MASK	%xmm10
#define CTR		%xmm11
#define INC		%xmm12

#define KEYP		%rdi
#define OUTP		%rsi
#define INP		%rdx
#define LEN		%rcx
#define HSTATE		%rcx
#define IVP		%r8
#define ICBP		%r8
#define KLEN		%r9d
#define T1		%r10
#define TKEYP		T1
#define T2		%r11
#define TCTR_LOW	T2

	.section .rodata
.align 16
.Lbswap_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

	.text

_key_expansion_128:
_key_expansion_256a:
	RETGUARD_SETUP(_key_expansion_128, rax)
	pshufd	$0b11111111,%xmm1,%xmm1
	shufps	$0b00010000,%xmm0,%xmm4
	pxor	%xmm4,%xmm0
	shufps	$0b10001100,%xmm0,%xmm4
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	movaps	%xmm0,(%rcx)
	add	$0x10,%rcx
	RETGUARD_CHECK(_key_expansion_128, rax)
	ret
	lfence

_key_expansion_192a:
	RETGUARD_SETUP(_key_expansion_192a, rax)
	pshufd	$0b01010101,%xmm1,%xmm1
	shufps	$0b00010000,%xmm0,%xmm4
	pxor	%xmm4,%xmm0
	shufps	$0b10001100,%xmm0,%xmm4
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0

	movaps	%xmm2,%xmm5
	movaps	%xmm2,%xmm6
	pslldq	$4,%xmm5
	pshufd	$0b11111111,%xmm0,%xmm3
	pxor	%xmm3,%xmm2
	pxor	%xmm5,%xmm2

	movaps	%xmm0,%xmm1
	shufps	$0b01000100,%xmm0,%xmm6
	movaps	%xmm6,(%rcx)
	shufps	$0b01001110,%xmm2,%xmm1
	movaps	%xmm1,16(%rcx)
	add	$0x20,%rcx
	RETGUARD_CHECK(_key_expansion_192a, rax)
	ret
	lfence

_key_expansion_192b:
	RETGUARD_SETUP(_key_expansion_192b, rax)
	pshufd	$0b01010101,%xmm1,%xmm1
	shufps	$0b00010000,%xmm0,%xmm4
	pxor	%xmm4,%xmm0
	shufps	$0b10001100,%xmm0,%xmm4
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0

	movaps	%xmm2,%xmm5
	pslldq	$4,%xmm5
	pshufd	$0b11111111,%xmm0,%xmm3
	pxor	%xmm3,%xmm2
	pxor	%xmm5,%xmm2

	movaps	%xmm0,(%rcx)
	add	$0x10,%rcx
	RETGUARD_CHECK(_key_expansion_192b, rax)
	ret
	lfence

_key_expansion_256b:
	RETGUARD_SETUP(_key_expansion_256b, rax)
	pshufd	$0b10101010,%xmm1,%xmm1
	shufps	$0b00010000,%xmm2,%xmm4
	pxor	%xmm4,%xmm2
	shufps	$0b10001100,%xmm2,%xmm4
	pxor	%xmm4,%xmm2
	pxor	%xmm1,%xmm2
	movaps	%xmm2,(%rcx)
	add	$0x10,%rcx
	RETGUARD_CHECK(_key_expansion_256b, rax)
	ret
	lfence

/*
 * void aesni_set_key(struct aesni_session *ses, uint8_t *key, size_t len)
 */
ENTRY(aesni_set_key)
	RETGUARD_SETUP(aesni_set_key, r11)
	movups	(%rsi),%xmm0		# user key (first 16 bytes)
	movaps	%xmm0,(%rdi)
	lea	0x10(%rdi),%rcx		# key addr
	movl	%edx,480(%rdi)
	pxor	%xmm4,%xmm4		# xmm4 is assumed 0 in _key_expansion_x
	cmp	$24,%dl
	jb	2f
	je	1f
	movups	0x10(%rsi),%xmm2	# other user key
	movaps	%xmm2,(%rcx)
	add	$0x10,%rcx
	aeskeygenassist $0x1,%xmm2,%xmm1	# round 1
	call	_key_expansion_256a
	aeskeygenassist $0x1,%xmm0,%xmm1
	call	_key_expansion_256b
	aeskeygenassist $0x2,%xmm2,%xmm1	# round 2
	call	_key_expansion_256a
	aeskeygenassist $0x2,%xmm0,%xmm1
	call	_key_expansion_256b
	aeskeygenassist $0x4,%xmm2,%xmm1	# round 3
	call	_key_expansion_256a
	aeskeygenassist $0x4,%xmm0,%xmm1
	call	_key_expansion_256b
	aeskeygenassist $0x8,%xmm2,%xmm1	# round 4
	call	_key_expansion_256a
	aeskeygenassist $0x8,%xmm0,%xmm1
	call	_key_expansion_256b
	aeskeygenassist $0x10,%xmm2,%xmm1	# round 5
	call	_key_expansion_256a
	aeskeygenassist $0x10,%xmm0,%xmm1
	call	_key_expansion_256b
	aeskeygenassist $0x20,%xmm2,%xmm1	# round 6
	call	_key_expansion_256a
	aeskeygenassist $0x20,%xmm0,%xmm1
	call	_key_expansion_256b
	aeskeygenassist $0x40,%xmm2,%xmm1	# round 7
	call	_key_expansion_256a
	jmp	3f
1:	/* 192 bit key */
	movq	0x10(%rsi),%xmm2	# other user key
	aeskeygenassist $0x1,%xmm2,%xmm1	# round 1
	call	_key_expansion_192a
	aeskeygenassist $0x2,%xmm2,%xmm1	# round 2
	call	_key_expansion_192b
	aeskeygenassist $0x4,%xmm2,%xmm1	# round 3
	call	_key_expansion_192a
	aeskeygenassist $0x8,%xmm2,%xmm1	# round 4
	call	_key_expansion_192b
	aeskeygenassist $0x10,%xmm2,%xmm1	# round 5
	call	_key_expansion_192a
	aeskeygenassist $0x20,%xmm2,%xmm1	# round 6
	call	_key_expansion_192b
	aeskeygenassist $0x40,%xmm2,%xmm1	# round 7
	call	_key_expansion_192a
	aeskeygenassist $0x80,%xmm2,%xmm1	# round 8
	call	_key_expansion_192b
	jmp	3f
2:	/* 128 bit key */
	aeskeygenassist $0x1,%xmm0,%xmm1	# round 1
	call	_key_expansion_128
	aeskeygenassist $0x2,%xmm0,%xmm1	# round 2
	call	_key_expansion_128
	aeskeygenassist $0x4,%xmm0,%xmm1	# round 3
	call	_key_expansion_128
	aeskeygenassist $0x8,%xmm0,%xmm1	# round 4
	call	_key_expansion_128
	aeskeygenassist $0x10,%xmm0,%xmm1	# round 5
	call	_key_expansion_128
	aeskeygenassist $0x20,%xmm0,%xmm1	# round 6
	call	_key_expansion_128
	aeskeygenassist $0x40,%xmm0,%xmm1	# round 7
	call	_key_expansion_128
	aeskeygenassist $0x80,%xmm0,%xmm1	# round 8
	call	_key_expansion_128
	aeskeygenassist $0x1b,%xmm0,%xmm1	# round 9
	call	_key_expansion_128
	aeskeygenassist $0x36,%xmm0,%xmm1	# round 10
	call	_key_expansion_128
3:
	sub	$0x10,%rcx
	movaps	(%rdi),%xmm0
	movaps	(%rcx),%xmm1
	movaps	%xmm0,240(%rcx)
	movaps	%xmm1,240(%rdi)
	add	$0x10,%rdi
	lea	240-16(%rcx),%rsi
.align 4
4:
	movaps	(%rdi),%xmm0
	aesimc	%xmm0,%xmm1
	movaps	%xmm1,(%rsi)
	add	$0x10,%rdi
	sub	$0x10,%rsi
	cmp	%rcx,%rdi
	jb	4b
	RETGUARD_CHECK(aesni_set_key, r11)
	ret
	lfence

/*
 * void aesni_enc(struct aesni_session *ses, uint8_t *dst, uint8_t *src)
 */
ENTRY(aesni_enc)
	RETGUARD_SETUP(aesni_enc, r11)
	movl	480(KEYP),KLEN		# key length
	movups	(INP),STATE		# input
	call	_aesni_enc1
	movups	STATE,(OUTP)		# output
	RETGUARD_CHECK(aesni_enc, r11)
	ret
	lfence

/*
 * _aesni_enc1:		internal ABI
 * input:
 *	KEYP:		key struct pointer
 *	KLEN:		round count
 *	STATE:		initial state (input)
 * output:
 *	STATE:		final state (output)
 * changed:
 *	KEY
 *	TKEYP (T1)
 */
_aesni_enc1:
	RETGUARD_SETUP(_aesni_enc1, rax)
	movaps	(KEYP),KEY		# key
	mov	KEYP,TKEYP
	pxor	KEY,STATE		# round 0
	add	$0x30,TKEYP
	cmp	$24,KLEN
	jb	2f
	lea	0x20(TKEYP),TKEYP
	je	1f
	add	$0x20,TKEYP
	movaps	-0x60(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	-0x50(TKEYP),KEY
	aesenc	KEY,STATE
.align 4
1:	/* 192 bit key */
	movaps	-0x40(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	-0x30(TKEYP),KEY
	aesenc	KEY,STATE
.align 4
2:	/* 128 bit key */
	movaps	-0x20(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	-0x10(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	0x10(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	0x20(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	0x30(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	0x40(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	0x50(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	0x60(TKEYP),KEY
	aesenc	KEY,STATE
	movaps	0x70(TKEYP),KEY
	aesenclast KEY,STATE
	RETGUARD_CHECK(_aesni_enc1, rax)
	ret
	lfence

/*
 * _aesni_enc4:	internal ABI
 * input:
 *	KEYP:		key struct pointer
 *	KLEN:		round count
 *	STATE1:		initial state (input)
 *	STATE2
 *	STATE3
 *	STATE4
 * output:
 *	STATE1:		final state (output)
 *	STATE2
 *	STATE3
 *	STATE4
 * changed:
 *	KEY
 *	TKEYP (T1)
 */
_aesni_enc4:
	RETGUARD_SETUP(_aesni_enc4, rax)
	movaps	(KEYP),KEY		# key
	mov	KEYP,TKEYP
	pxor	KEY,STATE1		# round 0
	pxor	KEY,STATE2
	pxor	KEY,STATE3
	pxor	KEY,STATE4
	add	$0x30,TKEYP
	cmp	$24,KLEN
	jb	2f
	lea	0x20(TKEYP),TKEYP
	je	1f
	add	$0x20,TKEYP
	movaps	-0x60(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	-0x50(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
#.align 4
1:	/* 192 bit key */
	movaps	-0x40(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	-0x30(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
#.align 4
2:	/* 128 bit key */
	movaps	-0x20(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	-0x10(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	0x10(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	0x20(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	0x30(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	0x40(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	0x50(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	0x60(TKEYP),KEY
	aesenc	KEY,STATE1
	aesenc	KEY,STATE2
	aesenc	KEY,STATE3
	aesenc	KEY,STATE4
	movaps	0x70(TKEYP),KEY
	aesenclast KEY,STATE1		# last round
	aesenclast KEY,STATE2
	aesenclast KEY,STATE3
	aesenclast KEY,STATE4
	RETGUARD_CHECK(_aesni_enc4, rax)
	ret
	lfence

/*
 * void aesni_dec(struct aesni_session *ses, uint8_t *dst, uint8_t *src)
 */
ENTRY(aesni_dec)
	RETGUARD_SETUP(aesni_dec, r11)
	mov	480(KEYP),KLEN		# key length
	add	$240,KEYP
	movups	(INP),STATE		# input
	call	_aesni_dec1
	movups	STATE,(OUTP)		# output
	RETGUARD_CHECK(aesni_dec, r11)
	ret
	lfence

/*
 * _aesni_dec1:		internal ABI
 * input:
 *	KEYP:		key struct pointer
 *	KLEN:		key length
 *	STATE:		initial state (input)
 * output:
 *	STATE:		final state (output)
 * changed:
 *	KEY
 *	TKEYP (T1)
 */
_aesni_dec1:
	RETGUARD_SETUP(_aesni_dec1, rax)
	movaps	(KEYP),KEY		# key
	mov	KEYP,TKEYP
	pxor	KEY,STATE		# round 0
	add	$0x30,TKEYP
	cmp	$24,KLEN
	jb	2f
	lea	0x20(TKEYP),TKEYP
	je	1f
	add	$0x20,TKEYP
	movaps	-0x60(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	-0x50(TKEYP),KEY
	aesdec	KEY,STATE
.align 4
1:	/* 192 bit key */
	movaps	-0x40(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	-0x30(TKEYP),KEY
	aesdec	KEY,STATE
.align 4
2:	/* 128 bit key */
	movaps	-0x20(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	-0x10(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	0x10(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	0x20(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	0x30(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	0x40(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	0x50(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	0x60(TKEYP),KEY
	aesdec	KEY,STATE
	movaps	0x70(TKEYP),KEY
	aesdeclast KEY,STATE
	RETGUARD_CHECK(_aesni_dec1, rax)
	ret
	lfence

/*
 * _aesni_dec4:	internal ABI
 * input:
 *	KEYP:		key struct pointer
 *	KLEN:		key length
 *	STATE1:		initial state (input)
 *	STATE2
 *	STATE3
 *	STATE4
 * output:
 *	STATE1:		final state (output)
 *	STATE2
 *	STATE3
 *	STATE4
 * changed:
 *	KEY
 *	TKEYP (T1)
 */
_aesni_dec4:
	RETGUARD_SETUP(_aesni_dec4, rax)
	movaps	(KEYP),KEY		# key
	mov	KEYP,TKEYP
	pxor	KEY,STATE1		# round 0
	pxor	KEY,STATE2
	pxor	KEY,STATE3
	pxor	KEY,STATE4
	add	$0x30,TKEYP
	cmp	$24,KLEN
	jb	2f
	lea	0x20(TKEYP),TKEYP
	je	1f
	add 	$0x20,TKEYP
	movaps	-0x60(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	-0x50(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
.align 4
1:	/* 192 bit key */
	movaps	-0x40(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	-0x30(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
.align 4
2:	/* 128 bit key */
	movaps	-0x20(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	-0x10(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	0x10(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	0x20(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	0x30(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	0x40(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	0x50(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	0x60(TKEYP),KEY
	aesdec	KEY,STATE1
	aesdec	KEY,STATE2
	aesdec	KEY,STATE3
	aesdec	KEY,STATE4
	movaps	0x70(TKEYP),KEY
	aesdeclast KEY,STATE1		# last round
	aesdeclast KEY,STATE2
	aesdeclast KEY,STATE3
	aesdeclast KEY,STATE4
	RETGUARD_CHECK(_aesni_dec4, rax)
	ret
	lfence

#if 0
/*
 * void aesni_ecb_enc(struct aesni_session *ses, uint8_t *dst, uint8_t *src,
 *     size_t len)
 */
ENTRY(aesni_ecb_enc)
	RETGUARD_SETUP(aesni_ecb_enc, r11)
	test	LEN,LEN			# check length
	jz	3f
	mov	480(KEYP),KLEN
	cmp	$16,LEN
	jb	3f
	cmp	$64,LEN
	jb	2f
.align 4
1:
	movups	(INP),STATE1
	movups	0x10(INP),STATE2
	movups	0x20(INP),STATE3
	movups	0x30(INP),STATE4
	call	_aesni_enc4
	movups	STATE1,(OUTP)
	movups	STATE2,0x10(OUTP)
	movups	STATE3,0x20(OUTP)
	movups	STATE4,0x30(OUTP)
	sub	$64,LEN
	add	$64,INP
	add	$64,OUTP
	cmp	$64,LEN
	jge	1b
	cmp	$16,LEN
	jb	3f
.align 4
2:
	movups	(INP),STATE1
	call	_aesni_enc1
	movups	STATE1,(OUTP)
	sub	$16,LEN
	add	$16,INP
	add	$16,OUTP
	cmp	$16,LEN
	jge	2b
3:
	RETGUARD_CHECK(aesni_ecb_enc, r11)
	ret
	lfence

/*
 * void aesni_ecb_dec(struct aesni_session *ses, uint8_t *dst, uint8_t *src,
 *     size_t len);
 */
ENTRY(aesni_ecb_dec)
	RETGUARD_SETUP(aesni_ecb_dec, r11)
	test	LEN,LEN
	jz	3f
	mov	480(KEYP),KLEN
	add	$240,KEYP
	cmp	$16,LEN
	jb	3f
	cmp	$64,LEN
	jb	2f
.align 4
1:
	movups	(INP),STATE1
	movups	0x10(INP),STATE2
	movups	0x20(INP),STATE3
	movups	0x30(INP),STATE4
	call	_aesni_dec4
	movups	STATE1,(OUTP)
	movups	STATE2,0x10(OUTP)
	movups	STATE3,0x20(OUTP)
	movups	STATE4,0x30(OUTP)
	sub	$64,LEN
	add	$64,INP
	add	$64,OUTP
	cmp	$64,LEN
	jge	1b
	cmp	$16,LEN
	jb	3f
.align 4
2:
	movups	(INP),STATE1
	call	_aesni_dec1
	movups	STATE1,(OUTP)
	sub	$16,LEN
	add	$16,INP
	add	$16,OUTP
	cmp	$16,LEN
	jge	2b
3:
	RETGUARD_CHECK(aesni_ecb_dec, r11)
	ret
	lfence
#endif

/*
 * void aesni_cbc_enc(struct aesni_session *ses, uint8_t *dst, uint8_t *src,
 *     size_t len, uint8_t *iv)
 */
ENTRY(aesni_cbc_enc)
	RETGUARD_SETUP(aesni_cbc_enc, r11)
	cmp	$16,LEN
	jb	2f
	mov	480(KEYP),KLEN
	movups	(IVP),STATE	# load iv as initial state
.align 4
1:
	movups	(INP),IN	# load input
	pxor	IN,STATE
	call	_aesni_enc1
	movups	STATE,(OUTP)	# store output
	sub	$16,LEN
	add	$16,INP
	add	$16,OUTP
	cmp	$16,LEN
	jge	1b
	movups	STATE,(IVP)
2:
	RETGUARD_CHECK(aesni_cbc_enc, r11)
	ret
	lfence

/*
 * void aesni_cbc_dec(struct aesni_session *ses, uint8_t *dst, uint8_t *src,
 *     size_t len, uint8_t *iv)
 */
ENTRY(aesni_cbc_dec)
	RETGUARD_SETUP(aesni_cbc_dec, r11)
	cmp	$16,LEN
	jb	4f
	mov	480(KEYP),KLEN
	add	$240,KEYP
	movups	(IVP),IV
	cmp	$64,LEN
	jb	2f
.align 4
1:	/* pipeline 4 instructions when possible */
	movups	(INP),IN1
	movaps	IN1,STATE1
	movups	0x10(INP),IN2
	movaps	IN2,STATE2
	movups	0x20(INP),IN3
	movaps	IN3,STATE3
	movups	0x30(INP),IN4
	movaps	IN4,STATE4
	call	_aesni_dec4
	pxor	IV,STATE1
	pxor	IN1,STATE2
	pxor	IN2,STATE3
	pxor	IN3,STATE4
	movaps	IN4,IV
	movups	STATE1,(OUTP)
	movups	STATE2,0x10(OUTP)
	movups	STATE3,0x20(OUTP)
	movups	STATE4,0x30(OUTP)
	sub	$64,LEN
	add	$64,INP
	add	$64,OUTP
	cmp	$64,LEN
	jge	1b
	cmp	$16,LEN
	jb	3f
.align 4
2:
	movups	(INP),IN
	movaps	IN,STATE
	call	_aesni_dec1
	pxor	IV,STATE
	movups	STATE,(OUTP)
	movaps	IN,IV
	sub	$16,LEN
	add	$16,INP
	add	$16,OUTP
	cmp	$16,LEN
	jge	2b
3:
	movups	IV,(IVP)
4:
	RETGUARD_CHECK(aesni_cbc_dec, r11)
	ret
	lfence

/*
 * _aesni_inc_init:	internal ABI
 *	setup registers used by _aesni_inc
 * input:
 *	ICB
 * output:
 *	CTR:		== CTR, in little endian
 *	IV:		== IV, in big endian
 *	TCTR_LOW:	== lower dword of CTR
 *	INC:		== 1, in little endian
 *	BSWAP_MASK	== endian swapping mask
 */
_aesni_inc_init:
	RETGUARD_SETUP(_aesni_inc_init, rax)
	movdqa	CTR,IV
	pslldq	$8,IV
	movdqu	.Lbswap_mask,BSWAP_MASK
	pshufb	BSWAP_MASK,CTR
	mov	$1,TCTR_LOW
	movd	TCTR_LOW,INC
	movd	CTR,TCTR_LOW
	RETGUARD_CHECK(_aesni_inc_init, rax)
	ret
	lfence

/*
 * _aesni_inc:		internal ABI
 *	Increase IV by 1, IV is in big endian
 * input:
 *	IV
 *	CTR:		== IV, in little endian
 *	TCTR_LOW:	== lower dword of CTR
 *	INC:		== 1, in little endian
 *	BSWAP_MASK	== endian swapping mask
 * output:
 *	IV:		Increase by 1
 * changed:
 *	CTR:		== output IV, in little endian
 *	TCTR_LOW:	== lower dword of CTR
 */
_aesni_inc:
	RETGUARD_SETUP(_aesni_inc, rax)
	paddq	INC,CTR
	add	$1,TCTR_LOW
	jnc	1f
	pslldq	$8,INC
	paddq	INC,CTR
	psrldq	$8,INC
1:
	movaps	CTR,IV
	pshufb	BSWAP_MASK,IV
	RETGUARD_CHECK(_aesni_inc, rax)
	ret
	lfence

/*
 * void aesni_ctr_enc(struct aesni_session *ses, uint8_t *dst, uint8_t *src,
 *     size_t len, uint8_t *icb)
 */
ENTRY(aesni_ctr_enc)
	RETGUARD_SETUP(aesni_ctr_enc, r11)
	RETGUARD_PUSH(r11)
	cmp	$16,LEN
	jb	4f
	mov	480(KEYP),KLEN
	movdqu	(ICBP),CTR
	call	_aesni_inc_init
	cmp	$64,LEN
	jb	2f
.align 4
1:	/* pipeline 4 instructions when possible */
	call	_aesni_inc
	movaps	IV,STATE1
	movups	(INP),IN1
	call	_aesni_inc
	movaps	IV,STATE2
	movups	0x10(INP),IN2
	call	_aesni_inc
	movaps	IV,STATE3
	movups	0x20(INP),IN3
	call	_aesni_inc
	movaps	IV,STATE4
	movups	0x30(INP),IN4
	call	_aesni_enc4
	pxor	IN1,STATE1
	movups	STATE1,(OUTP)
	pxor	IN2,STATE2
	movups	STATE2,0x10(OUTP)
	pxor	IN3,STATE3
	movups	STATE3,0x20(OUTP)
	pxor	IN4,STATE4
	movups	STATE4,0x30(OUTP)
	sub	$64,LEN
	add	$64,INP
	add	$64,OUTP
	cmp	$64,LEN
	jge	1b
	cmp	$16,LEN
	jb	3f
.align 4
2:
	call	_aesni_inc
	movaps	IV,STATE
	movups	(INP),IN
	call	_aesni_enc1
	pxor	IN,STATE
	movups	STATE,(OUTP)
	sub	$16,LEN
	add	$16,INP
	add	$16,OUTP
	cmp	$16,LEN
	jge	2b
3:
	movq	IV,(IVP)
4:
	RETGUARD_POP(r11)
	RETGUARD_CHECK(aesni_ctr_enc, r11)
	ret
	lfence

_aesni_gmac_gfmul:
	RETGUARD_SETUP(_aesni_gmac_gfmul, rax)
	movdqa	%xmm0,%xmm3
	pclmulqdq $0x00,%xmm1,%xmm3	# xmm3 holds a0*b0
	movdqa	%xmm0,%xmm4
	pclmulqdq $0x10,%xmm1,%xmm4	# xmm4 holds a0*b1
	movdqa	%xmm0,%xmm5
	pclmulqdq $0x01,%xmm1,%xmm5	# xmm5 holds a1*b0
	movdqa	%xmm0,%xmm6
	pclmulqdq $0x11,%xmm1,%xmm6	# xmm6 holds a1*b1

	pxor	%xmm5,%xmm4		# xmm4 holds a0*b1 + a1*b0
	movdqa	%xmm4,%xmm5
	psrldq	$8,%xmm4
	pslldq	$8,%xmm5
	pxor	%xmm5,%xmm3
	pxor	%xmm4,%xmm6

	/*
	 * <xmm6:xmm3> holds the result of the carry-less
	 * multiplication of xmm0 by xmm1
	 *
	 * shift the result by one bit position to the left
	 * cope for the fact that bits are reversed
	 */
	movdqa	%xmm3,%xmm7
	movdqa	%xmm6,%xmm8
	pslld	$1,%xmm3
	pslld	$1,%xmm6
	psrld	$31,%xmm7
	psrld	$31,%xmm8
	movdqa	%xmm7,%xmm9
	pslldq	$4,%xmm8
	pslldq	$4,%xmm7
	psrldq	$12,%xmm9
	por	%xmm7,%xmm3
	por	%xmm8,%xmm6
	por	%xmm9,%xmm6

	/* first phase of the reduction */
	movdqa	%xmm3,%xmm7
	movdqa	%xmm3,%xmm8
	movdqa	%xmm3,%xmm9
	pslld	$31,%xmm7		# packed right shifting << 31
	pslld	$30,%xmm8		# packed right shifting shift << 30
	pslld	$25,%xmm9		# packed right shifting shift << 25
	pxor	%xmm8,%xmm7		# xor the shifted versions
	pxor	%xmm9,%xmm7
	movdqa	%xmm7,%xmm8
	pslldq	$12,%xmm7
	psrldq	$4,%xmm8
	pxor	%xmm7,%xmm3

	/* second phase of the reduction */
	movdqa	%xmm3,%xmm2
	movdqa	%xmm3,%xmm4
	movdqa	%xmm3,%xmm5
	psrld	$1,%xmm2		# packed left shifting >> 1
	psrld	$2,%xmm4		# packed left shifting >> 2
	psrld	$7,%xmm5		# packed left shifting >> 7
	pxor	%xmm4,%xmm2		# xor the shifted versions
	pxor	%xmm5,%xmm2
	pxor	%xmm8,%xmm2
	pxor	%xmm2,%xmm3
	pxor	%xmm3,%xmm6		# the result is in xmm6
	RETGUARD_CHECK(_aesni_gmac_gfmul, rax)
	ret
	lfence

/*
 * void aesni_gmac_update(GHASH_CTX *ghash, uint8_t *src, size_t len)
 */
ENTRY(aesni_gmac_update)
	RETGUARD_SETUP(aesni_gmac_update, r11)
	cmp	$16,%rdx
	jb	2f

	movdqu	.Lbswap_mask,BSWAP_MASK	# endianness swap mask

	movdqu	(%rdi),%xmm1		# hash subkey
	movdqu	32(%rdi),%xmm6		# initial state
	pshufb	BSWAP_MASK,%xmm1
	pshufb	BSWAP_MASK,%xmm6
1:
	movdqu	(%rsi),%xmm2
	pshufb	BSWAP_MASK,%xmm2
	movdqa	%xmm6,%xmm0
	pxor	%xmm2,%xmm0
	call	_aesni_gmac_gfmul

	sub	$16,%rdx
	add	$16,%rsi
	cmp	$16,%rdx
	jge	1b

	pshufb	BSWAP_MASK,%xmm6
	movdqu	%xmm6,16(%rdi)
	movdqu	%xmm6,32(%rdi)
2:
	RETGUARD_CHECK(aesni_gmac_update, r11)
	ret
	lfence

/*
 * void aesni_gmac_final(struct aesni_sess *ses, uint8_t *tag,
 *     uint8_t *icb, uint8_t *hashstate)
 */
ENTRY(aesni_gmac_final)
	RETGUARD_SETUP(aesni_gmac_final, r11)
	movl	480(KEYP),KLEN		# key length
	movdqu	(INP),STATE		# icb
	call	_aesni_enc1
	movdqu	(HSTATE),IN
	pxor	IN,STATE
	movdqu	STATE,(OUTP)		# output
	RETGUARD_CHECK(aesni_gmac_final, r11)
	ret
	lfence

/*
 * void aesni_xts_enc(struct aesni_xts_ctx *xts, uint8_t *dst, uint8_t *src,
 *    size_t len, uint8_t *iv)
 */
ENTRY(aesni_xts_enc)
	RETGUARD_SETUP(aesni_xts_enc, r11)
	RETGUARD_PUSH(r11)
	cmp	$16,%rcx
	jb	2f

	call	_aesni_xts_tweak
	
	movl	480(KEYP),KLEN		# key length
1:
	movups	(%rdx),%xmm0		# src
	pxor	%xmm3,%xmm0		# xor block with tweak
	call	_aesni_enc1
	pxor	%xmm3,%xmm0		# xor block with tweak
	movups	%xmm0,(%rsi)		# dst

	call	_aesni_xts_tweak_exp

	add	$16,%rsi
	add	$16,%rdx
	sub	$16,%rcx
	cmp	$16,%rcx
	jge	1b
2:
	RETGUARD_POP(r11)
	RETGUARD_CHECK(aesni_xts_enc, r11)
	ret
	lfence

/*
 * void aesni_xts_dec(struct aesni_xts_ctx *xts, uint8_t *dst, uint8_t *src,
 *    size_t len, uint8_t *iv)
 */
ENTRY(aesni_xts_dec)
	RETGUARD_SETUP(aesni_xts_dec, r11)
	RETGUARD_PUSH(r11)
	cmp	$16,%rcx
	jb	2f

	call	_aesni_xts_tweak

	movl	480(KEYP),KLEN		# key length
	add	$240,KEYP		# decryption key
1:
	movups	(%rdx),%xmm0		# src
	pxor	%xmm3,%xmm0		# xor block with tweak
	call	_aesni_dec1
	pxor	%xmm3,%xmm0		# xor block with tweak
	movups	%xmm0,(%rsi)		# dst

	call	_aesni_xts_tweak_exp
	
	add	$16,%rsi
	add	$16,%rdx
	sub	$16,%rcx
	cmp	$16,%rcx
	jge	1b
2:
	RETGUARD_POP(r11)
	RETGUARD_CHECK(aesni_xts_dec, r11)
	ret
	lfence

/*
 * Prepare tweak as E_k2(IV). IV is specified as LE representation of a
 * 64-bit block number which we allow to be passed in directly. Since
 * we're on a 64-bit LE host the representation is already correct.
 *
 * xts is in %rdi, iv is in %r8 and we return the tweak in %xmm3.
 */
_aesni_xts_tweak:
	RETGUARD_SETUP(_aesni_xts_tweak, rax)
	RETGUARD_PUSH(rax)
	mov	(%r8),%r10
	movd	%r10,%xmm0		# Last 64-bits of IV are always zero.
	mov	KEYP,%r11
	lea	496(%rdi),KEYP
	movl	480(KEYP),KLEN
	call	_aesni_enc1
	movdqa	%xmm0,%xmm3
	mov	%r11,KEYP
	RETGUARD_POP(rax)
	RETGUARD_CHECK(_aesni_xts_tweak, rax)
	ret
	lfence

/*
 * Exponentiate AES XTS tweak (in %xmm3).
 */
_aesni_xts_tweak_exp:
	RETGUARD_SETUP(_aesni_xts_tweak_exp, rax)
	pextrw	$7,%xmm3,%r10
	pextrw	$3,%xmm3,%r11
	psllq	$1,%xmm3		# Left shift.

	and	$0x8000,%r11		# Carry between quads.
	jz	1f
	mov	$1,%r11
	pxor	%xmm0,%xmm0
	pinsrw	$4,%r11,%xmm0
	por	%xmm0,%xmm3
1:
	and	$0x8000,%r10
	jz	2f
	pextrw	$0,%xmm3,%r11
	xor	$0x87,%r11		# AES XTS alpha - GF(2^128).
	pinsrw	$0,%r11,%xmm3
2:
	RETGUARD_CHECK(_aesni_xts_tweak_exp, rax)
	ret
	lfence
