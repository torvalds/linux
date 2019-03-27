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
	movaps	%xmm0,(%edx)
	addl	$0x10,%edx
	retl
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
	movaps	%xmm6,(%edx)
	shufps	$0b01001110,%xmm2,%xmm1
	movaps	%xmm1,0x10(%edx)
	addl	$0x20,%edx
	retl
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
	movaps	%xmm0,(%edx)
	addl	$0x10,%edx
	retl
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
	movaps	%xmm2,(%edx)
	addl	$0x10,%edx
	retl
	.cfi_endproc
END(_key_expansion_256b)

ENTRY(aesni_set_enckey)
	.cfi_startproc
	pushl	%ebp
	.cfi_adjust_cfa_offset 4
	movl	%esp,%ebp
	movl	8(%ebp),%ecx
	movl	12(%ebp),%edx
	movups	(%ecx),%xmm0		# user key (first 16 bytes)
	movaps	%xmm0,(%edx)
	addl	$0x10,%edx		# key addr
	pxor	%xmm4,%xmm4		# xmm4 is assumed 0 in _key_expansion_x
	cmpl	$12,16(%ebp)		# rounds
	jb	.Lenc_key128
	je	.Lenc_key192
	movups	0x10(%ecx),%xmm2	# other user key
	movaps	%xmm2,(%edx)
	addl	$0x10,%edx
//	aeskeygenassist $0x1,%xmm2,%xmm1	# round 1
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x01
	call	_key_expansion_256a
//	aeskeygenassist $0x1,%xmm0,%xmm1
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x01
	call	_key_expansion_256b
//	aeskeygenassist $0x2,%xmm2,%xmm1	# round 2
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x02
	call	_key_expansion_256a
//	aeskeygenassist $0x2,%xmm0,%xmm1
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x02
	call	_key_expansion_256b
//	aeskeygenassist $0x4,%xmm2,%xmm1	# round 3
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x04
	call	_key_expansion_256a
//	aeskeygenassist $0x4,%xmm0,%xmm1
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x04
	call	_key_expansion_256b
//	aeskeygenassist $0x8,%xmm2,%xmm1	# round 4
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x08
	call	_key_expansion_256a
//	aeskeygenassist $0x8,%xmm0,%xmm1
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x08
	call	_key_expansion_256b
//	aeskeygenassist $0x10,%xmm2,%xmm1	# round 5
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x10
	call	_key_expansion_256a
//	aeskeygenassist $0x10,%xmm0,%xmm1
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x10
	call	_key_expansion_256b
//	aeskeygenassist $0x20,%xmm2,%xmm1	# round 6
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x20
	call	_key_expansion_256a
//	aeskeygenassist $0x20,%xmm0,%xmm1
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x20
	call	_key_expansion_256b
//	aeskeygenassist $0x40,%xmm2,%xmm1	# round 7
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x40
	call	_key_expansion_256a
	.cfi_adjust_cfa_offset -4
	leave
	retl
.Lenc_key192:
	movq	0x10(%ecx),%xmm2		# other user key
//	aeskeygenassist $0x1,%xmm2,%xmm1	# round 1
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x01
	call	_key_expansion_192a
//	aeskeygenassist $0x2,%xmm2,%xmm1	# round 2
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x02
	call	_key_expansion_192b
//	aeskeygenassist $0x4,%xmm2,%xmm1	# round 3
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x04
	call	_key_expansion_192a
//	aeskeygenassist $0x8,%xmm2,%xmm1	# round 4
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x08
	call	_key_expansion_192b
//	aeskeygenassist $0x10,%xmm2,%xmm1	# round 5
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x10
	call	_key_expansion_192a
//	aeskeygenassist $0x20,%xmm2,%xmm1	# round 6
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x20
	call	_key_expansion_192b
//	aeskeygenassist $0x40,%xmm2,%xmm1	# round 7
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x40
	call	_key_expansion_192a
//	aeskeygenassist $0x80,%xmm2,%xmm1	# round 8
	.byte	0x66,0x0f,0x3a,0xdf,0xca,0x80
	call	_key_expansion_192b
	leave
	.cfi_adjust_cfa_offset -4
	retl
.Lenc_key128:
//	aeskeygenassist $0x1,%xmm0,%xmm1	# round 1
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x01
	call	_key_expansion_128
//	aeskeygenassist $0x2,%xmm0,%xmm1	# round 2
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x02
	call	_key_expansion_128
//	aeskeygenassist $0x4,%xmm0,%xmm1	# round 3
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x04
	call	_key_expansion_128
//	aeskeygenassist $0x8,%xmm0,%xmm1	# round 4
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x08
	call	_key_expansion_128
//	aeskeygenassist $0x10,%xmm0,%xmm1	# round 5
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x10
	call	_key_expansion_128
//	aeskeygenassist $0x20,%xmm0,%xmm1	# round 6
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x20
	call	_key_expansion_128
//	aeskeygenassist $0x40,%xmm0,%xmm1	# round 7
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x40
	call	_key_expansion_128
//	aeskeygenassist $0x80,%xmm0,%xmm1	# round 8
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x80
	call	_key_expansion_128
//	aeskeygenassist $0x1b,%xmm0,%xmm1	# round 9
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x1b
	call	_key_expansion_128
//	aeskeygenassist $0x36,%xmm0,%xmm1	# round 10
	.byte	0x66,0x0f,0x3a,0xdf,0xc8,0x36
	call	_key_expansion_128
	leave
	.cfi_adjust_cfa_offset -4
	retl
	.cfi_endproc
END(aesni_set_enckey)

ENTRY(aesni_set_deckey)
	.cfi_startproc
	pushl	%ebp
	.cfi_adjust_cfa_offset 4
	movl	%esp,%ebp
	movl	16(%ebp),%eax	/* rounds */
	movl	%eax,%ecx
	shll	$4,%ecx
	addl	8(%ebp),%ecx	/* encrypt_schedule last quad */
	movl	12(%ebp),%edx	/* decrypt_schedule */
	movdqa	(%ecx),%xmm0
	movdqa	%xmm0,(%edx)
	decl	%eax
1:
	addl	$0x10,%edx
	subl	$0x10,%ecx
//	aesimc	(%ecx),%xmm1
	.byte	0x66,0x0f,0x38,0xdb,0x09
	movdqa	%xmm1,(%edx)
	decl	%eax
	jne	1b

	addl	$0x10,%edx
	subl	$0x10,%ecx
	movdqa	(%ecx),%xmm0
	movdqa	%xmm0,(%edx)
	leave
	.cfi_adjust_cfa_offset -4
	retl
	.cfi_endproc
END(aesni_set_deckey)

	.ident	"$FreeBSD$"
