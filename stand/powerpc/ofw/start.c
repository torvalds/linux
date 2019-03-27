/* $NetBSD: Locore.c,v 1.7 2000/08/20 07:04:59 tsubai Exp $ */
/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include "libofw.h"

void startup(void *, int, int (*)(void *), char *, int);

__asm("				\n\
	.data			\n\
	.align 4		\n\
stack:				\n\
	.space	16388		\n\
				\n\
	.text			\n\
	.globl	_start		\n\
_start:				\n\
	lis	%r1,stack@ha	\n\
	addi	%r1,%r1,stack@l	\n\
	addi	%r1,%r1,8192	\n\
				\n\
	/* Clear the .bss!!! */	\n\
	li      %r0,0		\n\
	lis     %r8,_edata@ha	\n\
	addi    %r8,%r8,_edata@l\n\
	lis     %r9,_end@ha	\n\
	addi    %r9,%r9,_end@l	\n\
				\n\
1:	cmpw    0,%r8,%r9	\n\
	bge     2f		\n\
	stw     %r0,0(%r8)	\n\
	addi    %r8,%r8,4	\n\
	b       1b		\n\
				\n\
2:	b	startup		\n\
");

void main(int (*openfirm)(void *));

void
startup(void *vpd, int res, int (*openfirm)(void *), char *arg, int argl)
{
	main(openfirm);
}
