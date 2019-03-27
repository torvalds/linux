/*	$NetBSD: bf_enc_686.S,v 1.1 2001/09/09 11:01:02 tls Exp $	*/
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
 * Modified from the output of `perl bf-686.pl elf' by
 * Jason R. Thorpe <thorpej@zembu.com> and Thor Lancelot Simon
 * <tls@netbsd.org>
 */

#include <i386/include/asm.h>
#define	_C_LABEL	CNAME

ENTRY(BF_encrypt)
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi


	/* Load the 2 words */
	movl	20(%esp),	%eax
	movl	(%eax),		%ecx
	movl	4(%eax),	%edx

	/* P pointer, s and enc flag */
	movl	24(%esp),	%edi
	xorl	%eax,		%eax
	xorl	%ebx,		%ebx
	xorl	(%edi),		%ecx

	/* Round 0 */
	rorl	$16,		%ecx
	movl	4(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 1 */
	rorl	$16,		%edx
	movl	8(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 2 */
	rorl	$16,		%ecx
	movl	12(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 3 */
	rorl	$16,		%edx
	movl	16(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 4 */
	rorl	$16,		%ecx
	movl	20(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 5 */
	rorl	$16,		%edx
	movl	24(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 6 */
	rorl	$16,		%ecx
	movl	28(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 7 */
	rorl	$16,		%edx
	movl	32(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 8 */
	rorl	$16,		%ecx
	movl	36(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 9 */
	rorl	$16,		%edx
	movl	40(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 10 */
	rorl	$16,		%ecx
	movl	44(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 11 */
	rorl	$16,		%edx
	movl	48(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 12 */
	rorl	$16,		%ecx
	movl	52(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 13 */
	rorl	$16,		%edx
	movl	56(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 14 */
	rorl	$16,		%ecx
	movl	60(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 15 */
	rorl	$16,		%edx
	movl	64(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx
	xorl	68(%edi),	%edx
	movl	20(%esp),	%eax
	movl	%edx,		(%eax)
	movl	%ecx,		4(%eax)
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
	pushl	%esi
	pushl	%edi


	/* Load the 2 words */
	movl	20(%esp),	%eax
	movl	(%eax),		%ecx
	movl	4(%eax),	%edx

	/* P pointer, s and enc flag */
	movl	24(%esp),	%edi
	xorl	%eax,		%eax
	xorl	%ebx,		%ebx
	xorl	68(%edi),	%ecx

	/* Round 16 */
	rorl	$16,		%ecx
	movl	64(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 15 */
	rorl	$16,		%edx
	movl	60(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 14 */
	rorl	$16,		%ecx
	movl	56(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 13 */
	rorl	$16,		%edx
	movl	52(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 12 */
	rorl	$16,		%ecx
	movl	48(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 11 */
	rorl	$16,		%edx
	movl	44(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 10 */
	rorl	$16,		%ecx
	movl	40(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 9 */
	rorl	$16,		%edx
	movl	36(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 8 */
	rorl	$16,		%ecx
	movl	32(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 7 */
	rorl	$16,		%edx
	movl	28(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 6 */
	rorl	$16,		%ecx
	movl	24(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 5 */
	rorl	$16,		%edx
	movl	20(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 4 */
	rorl	$16,		%ecx
	movl	16(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 3 */
	rorl	$16,		%edx
	movl	12(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx

	/* Round 2 */
	rorl	$16,		%ecx
	movl	8(%edi),	%esi
	movb	%ch,		%al
	movb	%cl,		%bl
	rorl	$16,		%ecx
	xorl	%esi,		%edx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%ch,		%al
	movb	%cl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%edx

	/* Round 1 */
	rorl	$16,		%edx
	movl	4(%edi),	%esi
	movb	%dh,		%al
	movb	%dl,		%bl
	rorl	$16,		%edx
	xorl	%esi,		%ecx
	movl	72(%edi,%eax,4),%esi
	movl	1096(%edi,%ebx,4),%ebp
	movb	%dh,		%al
	movb	%dl,		%bl
	addl	%ebp,		%esi
	movl	2120(%edi,%eax,4),%eax
	xorl	%eax,		%esi
	movl	3144(%edi,%ebx,4),%ebp
	addl	%ebp,		%esi
	xorl	%eax,		%eax
	xorl	%esi,		%ecx
	xorl	(%edi),		%edx
	movl	20(%esp),	%eax
	movl	%edx,		(%eax)
	movl	%ecx,		4(%eax)
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
	.L_BF_decrypt_end:
	.size   _C_LABEL(BF_decrypt),.L_BF_decrypt_end-_C_LABEL(BF_decrypt)
