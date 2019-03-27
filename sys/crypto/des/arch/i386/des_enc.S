/* $NetBSD: des_enc.S,v 1.1 2001/09/09 11:01:02 tls Exp $ */
/* $FreeBSD$ */

/* Copyright (C) 1995-1997 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

/*
 * Modified from the output of `perl des686.pl elf' by 
 * Thor Lancelot Simon <tls@netbsd.org>
 */

#include <i386/include/asm.h>
#define	_C_LABEL	CNAME

ENTRY(des_encrypt1)
	pushl	%esi
	pushl	%edi

	/* Load the 2 words */
	movl	12(%esp),	%esi
	xorl	%ecx,		%ecx
	pushl	%ebx
	pushl	%ebp
	movl	(%esi),		%eax
	movl	28(%esp),	%ebx
	movl	4(%esi),	%edi

	/* IP */
	roll	$4,		%eax
	movl	%eax,		%esi
	xorl	%edi,		%eax
	andl	$0xf0f0f0f0,	%eax
	xorl	%eax,		%esi
	xorl	%eax,		%edi

	roll	$20,		%edi
	movl	%edi,		%eax
	xorl	%esi,		%edi
	andl	$0xfff0000f,	%edi
	xorl	%edi,		%eax
	xorl	%edi,		%esi

	roll	$14,		%eax
	movl	%eax,		%edi
	xorl	%esi,		%eax
	andl	$0x33333333,	%eax
	xorl	%eax,		%edi
	xorl	%eax,		%esi

	roll	$22,		%esi
	movl	%esi,		%eax
	xorl	%edi,		%esi
	andl	$0x03fc03fc,	%esi
	xorl	%esi,		%eax
	xorl	%esi,		%edi

	roll	$9,		%eax
	movl	%eax,		%esi
	xorl	%edi,		%eax
	andl	$0xaaaaaaaa,	%eax
	xorl	%eax,		%esi
	xorl	%eax,		%edi

.byte 209
.byte 199		# roll $1 %edi 
	movl	24(%esp),	%ebp
	cmpl	$0,		%ebx
	je	.L000start_decrypt

	/* Round 0 */
	movl	(%ebp),		%eax
	xorl	%ebx,		%ebx
	movl	4(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 1 */
	movl	8(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	12(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 2 */
	movl	16(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	20(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 3 */
	movl	24(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	28(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 4 */
	movl	32(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	36(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 5 */
	movl	40(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	44(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 6 */
	movl	48(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	52(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 7 */
	movl	56(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	60(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 8 */
	movl	64(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	68(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 9 */
	movl	72(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	76(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 10 */
	movl	80(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	84(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 11 */
	movl	88(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	92(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 12 */
	movl	96(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	100(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 13 */
	movl	104(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	108(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 14 */
	movl	112(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	116(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 15 */
	movl	120(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	124(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi
	jmp	.L001end
.L000start_decrypt:

	/* Round 15 */
	movl	120(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	124(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 14 */
	movl	112(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	116(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 13 */
	movl	104(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	108(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 12 */
	movl	96(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	100(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 11 */
	movl	88(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	92(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 10 */
	movl	80(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	84(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 9 */
	movl	72(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	76(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 8 */
	movl	64(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	68(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 7 */
	movl	56(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	60(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 6 */
	movl	48(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	52(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 5 */
	movl	40(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	44(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 4 */
	movl	32(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	36(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 3 */
	movl	24(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	28(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 2 */
	movl	16(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	20(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 1 */
	movl	8(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	12(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 0 */
	movl	(%ebp),		%eax
	xorl	%ebx,		%ebx
	movl	4(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi
.L001end:

	/* FP */
	movl	20(%esp),	%edx
.byte 209
.byte 206		# rorl $1 %esi 
	movl	%edi,		%eax
	xorl	%esi,		%edi
	andl	$0xaaaaaaaa,	%edi
	xorl	%edi,		%eax
	xorl	%edi,		%esi

	roll	$23,		%eax
	movl	%eax,		%edi
	xorl	%esi,		%eax
	andl	$0x03fc03fc,	%eax
	xorl	%eax,		%edi
	xorl	%eax,		%esi

	roll	$10,		%edi
	movl	%edi,		%eax
	xorl	%esi,		%edi
	andl	$0x33333333,	%edi
	xorl	%edi,		%eax
	xorl	%edi,		%esi

	roll	$18,		%esi
	movl	%esi,		%edi
	xorl	%eax,		%esi
	andl	$0xfff0000f,	%esi
	xorl	%esi,		%edi
	xorl	%esi,		%eax

	roll	$12,		%edi
	movl	%edi,		%esi
	xorl	%eax,		%edi
	andl	$0xf0f0f0f0,	%edi
	xorl	%edi,		%esi
	xorl	%edi,		%eax

	rorl	$4,		%eax
	movl	%eax,		(%edx)
	movl	%esi,		4(%edx)
	popl	%ebp
	popl	%ebx
	popl	%edi
	popl	%esi
	ret
.L_des_encrypt1_end:
	.size	_C_LABEL(des_encrypt1),.L_des_encrypt1_end-_C_LABEL(des_encrypt1)

ENTRY(des_encrypt2)
	pushl	%esi
	pushl	%edi

	/* Load the 2 words */
	movl	12(%esp),	%eax
	xorl	%ecx,		%ecx
	pushl	%ebx
	pushl	%ebp
	movl	(%eax),		%esi
	movl	28(%esp),	%ebx
	roll	$3,		%esi
	movl	4(%eax),	%edi
	roll	$3,		%edi
	movl	24(%esp),	%ebp
	cmpl	$0,		%ebx
	je	.L002start_decrypt

	/* Round 0 */
	movl	(%ebp),		%eax
	xorl	%ebx,		%ebx
	movl	4(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 1 */
	movl	8(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	12(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 2 */
	movl	16(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	20(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 3 */
	movl	24(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	28(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 4 */
	movl	32(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	36(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 5 */
	movl	40(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	44(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 6 */
	movl	48(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	52(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 7 */
	movl	56(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	60(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 8 */
	movl	64(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	68(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 9 */
	movl	72(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	76(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 10 */
	movl	80(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	84(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 11 */
	movl	88(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	92(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 12 */
	movl	96(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	100(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 13 */
	movl	104(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	108(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 14 */
	movl	112(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	116(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 15 */
	movl	120(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	124(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi
	jmp	.L003end
.L002start_decrypt:

	/* Round 15 */
	movl	120(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	124(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 14 */
	movl	112(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	116(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 13 */
	movl	104(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	108(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 12 */
	movl	96(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	100(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 11 */
	movl	88(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	92(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 10 */
	movl	80(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	84(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 9 */
	movl	72(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	76(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 8 */
	movl	64(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	68(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 7 */
	movl	56(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	60(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 6 */
	movl	48(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	52(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 5 */
	movl	40(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	44(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 4 */
	movl	32(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	36(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 3 */
	movl	24(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	28(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 2 */
	movl	16(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	20(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi

	/* Round 1 */
	movl	8(%ebp),	%eax
	xorl	%ebx,		%ebx
	movl	12(%ebp),	%edx
	xorl	%esi,		%eax
	xorl	%esi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%edi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%edi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%edi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%edi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%edi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%edi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%edi

	/* Round 0 */
	movl	(%ebp),		%eax
	xorl	%ebx,		%ebx
	movl	4(%ebp),	%edx
	xorl	%edi,		%eax
	xorl	%edi,		%edx
	andl	$0xfcfcfcfc,	%eax
	andl	$0xcfcfcfcf,	%edx
	movb	%al,		%bl
	movb	%ah,		%cl
	rorl	$4,		%edx
	movl	      _C_LABEL(des_SPtrans)(%ebx),%ebp
	movb	%dl,		%bl
	xorl	%ebp,		%esi
	movl	0x200+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movb	%dh,		%cl
	shrl	$16,		%eax
	movl	0x100+_C_LABEL(des_SPtrans)(%ebx),%ebp
	xorl	%ebp,		%esi
	movb	%ah,		%bl
	shrl	$16,		%edx
	movl	0x300+_C_LABEL(des_SPtrans)(%ecx),%ebp
	xorl	%ebp,		%esi
	movl	24(%esp),	%ebp
	movb	%dh,		%cl
	andl	$0xff,		%eax
	andl	$0xff,		%edx
	movl	0x600+_C_LABEL(des_SPtrans)(%ebx),%ebx
	xorl	%ebx,		%esi
	movl	0x700+_C_LABEL(des_SPtrans)(%ecx),%ebx
	xorl	%ebx,		%esi
	movl	0x400+_C_LABEL(des_SPtrans)(%eax),%ebx
	xorl	%ebx,		%esi
	movl	0x500+_C_LABEL(des_SPtrans)(%edx),%ebx
	xorl	%ebx,		%esi
.L003end:

	/* Fixup */
	rorl	$3,		%edi
	movl	20(%esp),	%eax
	rorl	$3,		%esi
	movl	%edi,		(%eax)
	movl	%esi,		4(%eax)
	popl	%ebp
	popl	%ebx
	popl	%edi
	popl	%esi
	ret
.L_des_encrypt2_end:
	.size	_C_LABEL(des_encrypt2),.L_des_encrypt2_end-_C_LABEL(des_encrypt2)

ENTRY(des_encrypt3)
	pushl   %ebx
	movl    8(%esp),        %ebx
	pushl   %ebp
	pushl   %esi
	pushl   %edi

	/* Load the data words */
	movl    (%ebx),         %edi
	movl    4(%ebx),        %esi
	subl    $12,            %esp

	/* IP */
	roll    $4,             %edi
	movl    %edi,           %edx
	xorl    %esi,           %edi
	andl    $0xf0f0f0f0,    %edi
	xorl    %edi,           %edx
	xorl    %edi,           %esi

	roll    $20,            %esi
	movl    %esi,           %edi
	xorl    %edx,           %esi
	andl    $0xfff0000f,    %esi
	xorl    %esi,           %edi
	xorl    %esi,           %edx

	roll    $14,            %edi
	movl    %edi,           %esi
	xorl    %edx,           %edi
	andl    $0x33333333,    %edi
	xorl    %edi,           %esi
	xorl    %edi,           %edx

	roll    $22,            %edx
	movl    %edx,           %edi
	xorl    %esi,           %edx
	andl    $0x03fc03fc,    %edx
	xorl    %edx,           %edi
	xorl    %edx,           %esi

	roll    $9,             %edi
	movl    %edi,           %edx
	xorl    %esi,           %edi
	andl    $0xaaaaaaaa,    %edi
	xorl    %edi,           %edx
	xorl    %edi,           %esi

	rorl    $3,             %edx
	rorl    $2,             %esi
	movl    %esi,           4(%ebx)
	movl    36(%esp),       %eax
	movl    %edx,           (%ebx)
	movl    40(%esp),       %edi
	movl    44(%esp),       %esi
	movl    $1,             8(%esp)
	movl    %eax,           4(%esp)
	movl    %ebx,           (%esp)
	call    _C_LABEL(des_encrypt2)
	movl    $0,             8(%esp)
	movl    %edi,           4(%esp)
	movl    %ebx,           (%esp)
	call    _C_LABEL(des_encrypt2)
	movl    $1,             8(%esp)
	movl    %esi,           4(%esp)
	movl    %ebx,           (%esp)
	call    _C_LABEL(des_encrypt2)
	addl    $12,            %esp
	movl    (%ebx),         %edi
	movl    4(%ebx),        %esi

	/* FP */
	roll    $2,             %esi
	roll    $3,             %edi
	movl    %edi,           %eax
	xorl    %esi,           %edi
	andl    $0xaaaaaaaa,    %edi
	xorl    %edi,           %eax
	xorl    %edi,           %esi

	roll    $23,            %eax
	movl    %eax,           %edi
	xorl    %esi,           %eax
	andl    $0x03fc03fc,    %eax
	xorl    %eax,           %edi
	xorl    %eax,           %esi

	roll    $10,            %edi
	movl    %edi,           %eax
	xorl    %esi,           %edi
	andl    $0x33333333,    %edi
	xorl    %edi,           %eax
	xorl    %edi,           %esi

	roll    $18,            %esi
	movl    %esi,           %edi
	xorl    %eax,           %esi
	andl    $0xfff0000f,    %esi
	xorl    %esi,           %edi
	xorl    %esi,           %eax

	roll    $12,            %edi
	movl    %edi,           %esi
	xorl    %eax,           %edi
	andl    $0xf0f0f0f0,    %edi
	xorl    %edi,           %esi
	xorl    %edi,           %eax

	rorl    $4,             %eax
	movl    %eax,           (%ebx)
	movl    %esi,           4(%ebx)
	popl    %edi
	popl    %esi
	popl    %ebp
	popl    %ebx
	ret
.L_des_encrypt3_end:
        .size   _C_LABEL(des_encrypt3),.L_des_encrypt3_end-_C_LABEL(des_encrypt3)

ENTRY(des_decrypt3)
	pushl   %ebx
	movl    8(%esp),        %ebx
	pushl   %ebp
	pushl   %esi
	pushl   %edi

	/* Load the data words */
	movl    (%ebx),         %edi
	movl    4(%ebx),        %esi
	subl    $12,            %esp

	/* IP */
	roll    $4,             %edi
	movl    %edi,           %edx
	xorl    %esi,           %edi
	andl    $0xf0f0f0f0,    %edi
	xorl    %edi,           %edx
	xorl    %edi,           %esi

	roll    $20,            %esi
	movl    %esi,           %edi
	xorl    %edx,           %esi
	andl    $0xfff0000f,    %esi
	xorl    %esi,           %edi
	xorl    %esi,           %edx

	roll    $14,            %edi
	movl    %edi,           %esi
	xorl    %edx,           %edi
	andl    $0x33333333,    %edi
	xorl    %edi,           %esi
	xorl    %edi,           %edx

	roll    $22,            %edx
	movl    %edx,           %edi
	xorl    %esi,           %edx
	andl    $0x03fc03fc,    %edx
	xorl    %edx,           %edi
	xorl    %edx,           %esi

	roll    $9,             %edi
	movl    %edi,           %edx
	xorl    %esi,           %edi
	andl    $0xaaaaaaaa,    %edi
	xorl    %edi,           %edx
	xorl    %edi,           %esi

	rorl    $3,             %edx
	rorl    $2,             %esi
	movl    %esi,           4(%ebx)
	movl    36(%esp),       %esi
	movl    %edx,           (%ebx)
	movl    40(%esp),       %edi
	movl    44(%esp),       %eax
	movl    $0,             8(%esp)
	movl    %eax,           4(%esp)
	movl    %ebx,           (%esp)
	call    _C_LABEL(des_encrypt2)
	movl    $1,             8(%esp)
	movl    %edi,           4(%esp)
	movl    %ebx,           (%esp)
	call    _C_LABEL(des_encrypt2)
	movl    $0,             8(%esp)
	movl    %esi,           4(%esp)
	movl    %ebx,           (%esp)
	call    _C_LABEL(des_encrypt2)
	addl    $12,            %esp
	movl    (%ebx),         %edi
	movl    4(%ebx),        %esi

	/* FP */
	roll    $2,             %esi
	roll    $3,             %edi
	movl    %edi,           %eax
	xorl    %esi,           %edi
	andl    $0xaaaaaaaa,    %edi
	xorl    %edi,           %eax
	xorl    %edi,           %esi

	roll    $23,            %eax
	movl    %eax,           %edi
	xorl    %esi,           %eax
	andl    $0x03fc03fc,    %eax
	xorl    %eax,           %edi
	xorl    %eax,           %esi

	roll    $10,            %edi
	movl    %edi,           %eax
	xorl    %esi,           %edi
	andl    $0x33333333,    %edi
	xorl    %edi,           %eax
	xorl    %edi,           %esi

	roll    $18,            %esi
	movl    %esi,           %edi
	xorl    %eax,           %esi
	andl    $0xfff0000f,    %esi
	xorl    %esi,           %edi
	xorl    %esi,           %eax

	roll    $12,            %edi
	movl    %edi,           %esi
	xorl    %eax,           %edi
	andl    $0xf0f0f0f0,    %edi
	xorl    %edi,           %esi
	xorl    %edi,           %eax

        rorl    $4,             %eax
	movl    %eax,           (%ebx)
	movl    %esi,           4(%ebx)
	popl    %edi
	popl    %esi
	popl    %ebp
	popl    %ebx
	ret
.L_des_decrypt3_end:
	.size   _C_LABEL(des_decrypt3),.L_des_decrypt3_end-_C_LABEL(des_decrypt3)
