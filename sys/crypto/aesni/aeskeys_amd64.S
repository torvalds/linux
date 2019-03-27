/*-
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

#include <machine/asmacros.h>

	.text

ENTRY(_key_expansion_128)
_key_expansion_256a:
	.cfi_startproc
	pshufd	$0b11111111,%xmm1,%xmm1
	shufps	$0b00010000,%xmm0,%xmm4
	pxor	%xmm4,%xmm0
	shufps	$0b10001100,%xmm0,%xmm4
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	movaps	%xmm0,(%rsi)
	addq	$0x10,%rsi
	retq
	.cfi_endproc
END(_key_expansion_128)

ENTRY(_key_expansion_192a)
	.cfi_startproc
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
	movaps	%xmm6,(%rsi)
	shufps	$0b01001110,%xmm2,%xmm1
	movaps	%xmm1,0x10(%rsi)
	addq	$0x20,%rsi
	retq
	.cfi_endproc
END(_key_expansion_192a)

ENTRY(_key_expansion_192b)
	.cfi_startproc
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
	movaps	%xmm0,(%rsi)
	addq	$0x10,%rsi
	retq
	.cfi_endproc
END(_key_expansion_192b)

ENTRY(_key_expansion_256b)
	.cfi_startproc
	pshufd	$0b10101010,%xmm1,%xmm1
	shufps	$0b00010000,%xmm2,%xmm4
	pxor	%xmm4,%xmm2
	shufps	$0b10001100,%xmm2,%xmm4
	pxor	%xmm4,%xmm2
	pxor	%xmm1,%xmm2
	movaps	%xmm2,(%rsi)
	addq	$0x10,%rsi
	retq
	.cfi_endproc
END(_key_expansion_256b)

ENTRY(aesni_set_enckey)
	.cfi_startproc
	movups	(%rdi),%xmm0		# user key (first 16 bytes)
	movaps	%xmm0,(%rsi)
	addq	$0x10,%rsi		# key addr
	pxor	%xmm4,%xmm4		# xmm4 is assumed 0 in _key_expansion_x
	cmpl	$12,%edx
	jb	.Lenc_key128
	je	.Lenc_key192
	movups	0x10(%rdi),%xmm2	# other user key
	movaps	%xmm2,(%rsi)
	addq	$0x10,%rsi
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
	retq
.Lenc_key192:
	movq	0x10(%rdi),%xmm2		# other user key
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
	retq
.Lenc_key128:
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
	retq
	.cfi_endproc
END(aesni_set_enckey)

ENTRY(aesni_set_deckey)
	.cfi_startproc
	movslq	%edx,%rax
	shlq	$4,%rax
	addq	%rax,%rdi
	movdqa	(%rdi),%xmm0
	movdqa	%xmm0,(%rsi)
	decl	%edx
1:
	addq	$0x10,%rsi
	subq	$0x10,%rdi
	aesimc	(%rdi),%xmm1
	movdqa	%xmm1,(%rsi)
	decl	%edx
	jne	1b

	addq	$0x10,%rsi
	subq	$0x10,%rdi
	movdqa	(%rdi),%xmm0
	movdqa	%xmm0,(%rsi)
	retq
	.cfi_endproc
END(aesni_set_deckey)

	.ident	"$FreeBSD$"
