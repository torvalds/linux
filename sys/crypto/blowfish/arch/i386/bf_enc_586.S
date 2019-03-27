/*	$NetBSD: bf_enc_586.S,v 1.1 2001/09/09 11:01:01 tls Exp $	*/
/*	$FreeBSD$	*/

/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
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
 * Modified from the output of `perl bf-586.pl elf' by
 * Jason R. Thorpe <thorpej@zembu.com> and Thor Lancelot Simon
 * <tls@netbsd.org>
 */

#include <i386/include/asm.h>
#define	_C_LABEL	CNAME

ENTRY(BF_encrypt)
	pushl	%ebp
	pushl	%ebx
	movl	12(%esp),	%ebx
	movl	16(%esp),	%ebp
	pushl	%esi
	pushl	%edi
	/* Load the 2 words */
	movl	(%ebx),		%edi
	movl	4(%ebx),	%esi
	xorl	%eax,		%eax
	movl	(%ebp),		%ebx
	xorl	%ecx,		%ecx
	xorl	%ebx,		%edi

	/* Round 0 */
	movl	4(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 1 */
	movl	8(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 2 */
	movl	12(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 3 */
	movl	16(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 4 */
	movl	20(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 5 */
	movl	24(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 6 */
	movl	28(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 7 */
	movl	32(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 8 */
	movl	36(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 9 */
	movl	40(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 10 */
	movl	44(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 11 */
	movl	48(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 12 */
	movl	52(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 13 */
	movl	56(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 14 */
	movl	60(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 15 */
	movl	64(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	/* Load parameter 0 (16) enc=1 */
	movl	20(%esp),	%eax
	xorl	%ebx,		%edi
	movl	68(%ebp),	%edx
	xorl	%edx,		%esi
	movl	%edi,		4(%eax)
	movl	%esi,		(%eax)
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.L_BF_encrypt_end:
	.size   _C_LABEL(BF_encrypt),.L_BF_encrypt_end-_C_LABEL(BF_encrypt)

ENTRY(BF_decrypt)
	pushl	%ebp
	pushl	%ebx
	movl	12(%esp),	%ebx
	movl	16(%esp),	%ebp
	pushl	%esi
	pushl	%edi
	/* Load the 2 words */
	movl	(%ebx),		%edi
	movl	4(%ebx),	%esi
	xorl	%eax,		%eax
	movl	68(%ebp),	%ebx
	xorl	%ecx,		%ecx
	xorl	%ebx,		%edi

	/* Round 16 */
	movl	64(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 15 */
	movl	60(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 14 */
	movl	56(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 13 */
	movl	52(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 12 */
	movl	48(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 11 */
	movl	44(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 10 */
	movl	40(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 9 */
	movl	36(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 8 */
	movl	32(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 7 */
	movl	28(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 6 */
	movl	24(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 5 */
	movl	20(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 4 */
	movl	16(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 3 */
	movl	12(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%edi

	/* Round 2 */
	movl	8(%ebp),	%edx
	movl	%edi,		%ebx
	xorl	%edx,		%esi
	shrl	$16,		%ebx
	movl	%edi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	xorl	%eax,		%eax
	xorl	%ebx,		%esi

	/* Round 1 */
	movl	4(%ebp),	%edx
	movl	%esi,		%ebx
	xorl	%edx,		%edi
	shrl	$16,		%ebx
	movl	%esi,		%edx
	movb	%bh,		%al
	andl	$255,		%ebx
	movb	%dh,		%cl
	andl	$255,		%edx
	movl	72(%ebp,%eax,4),%eax
	movl	1096(%ebp,%ebx,4),%ebx
	addl	%eax,		%ebx
	movl	2120(%ebp,%ecx,4),%eax
	xorl	%eax,		%ebx
	movl	3144(%ebp,%edx,4),%edx
	addl	%edx,		%ebx
	/* Load parameter 0 (1) enc=0 */
	movl	20(%esp),	%eax
	xorl	%ebx,		%edi
	movl	(%ebp),		%edx
	xorl	%edx,		%esi
	movl	%edi,		4(%eax)
	movl	%esi,		(%eax)
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.L_BF_decrypt_end:
	.size   _C_LABEL(BF_decrypt),.L_BF_decrypt_end-_C_LABEL(BF_decrypt)
