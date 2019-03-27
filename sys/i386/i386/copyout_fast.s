/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <machine/asmacros.h>
#include <machine/cputypes.h>
#include <machine/pmap.h>
#include <machine/specialreg.h>

#include "assym.inc"

	.text

ENTRY(copyout_fast)
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	$copyout_fault,%edx
	movl	20(%ebp),%ebx	/* KCR3 */

	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax),%edi

	cli
	movl	PCPU(TRAMPSTK),%esi
	movl	PCPU(COPYOUT_BUF),%eax
	subl	$4,%esi
	movl	%eax,(%esi)
	movl	12(%ebp),%eax	/* udaddr */
	subl	$4,%esi
	movl	%eax,(%esi)
	movl	16(%ebp),%eax	/* len */
	subl	$4,%esi
	movl	%eax,(%esi)

	subl	$4, %esi
	movl	%edi, (%esi)

	movl	8(%ebp),%eax	/* kaddr */
	subl	$4,%esi
	movl	%eax,(%esi)
	movl	PCPU(COPYOUT_BUF),%eax
	subl	$4,%esi
	movl	%eax,(%esi)
	movl	16(%ebp),%eax	/* len */
	subl	$4,%esi
	movl	%eax,(%esi)

	movl	%esp,%eax
	movl	%esi,%esp

	/* bcopy(%esi = kaddr, %edi = PCPU(copyout_buf), %ecx = len) */
	popl	%ecx
	popl	%edi
	popl	%esi
	rep; movsb

	popl	%edi
	movl	%edi,%cr3

	/* bcopy(%esi = PCPU(copyout_buf), %edi = udaddr, %ecx = len) */
	popl	%ecx
	popl	%edi
	popl	%esi
	rep; movsb

	movl	%ebx,%cr3
	movl	%eax,%esp
	sti

	xorl	%eax,%eax
	popl	%ebx
	popl	%edi
	popl	%esi
	leave
	ret
END(copyout_fast)

ENTRY(copyin_fast)
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	$copyout_fault,%edx
	movl	20(%ebp),%ebx	/* KCR3 */

	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax),%edi

	cli
	movl	PCPU(TRAMPSTK),%esi
	movl	PCPU(COPYOUT_BUF),%eax
	subl	$4,%esi
	movl	%eax,(%esi)
	movl	12(%ebp),%eax	/* kaddr */
	subl	$4,%esi
	movl	%eax,(%esi)
	movl	16(%ebp),%eax	/* len */
	subl	$4,%esi
	movl	%eax,(%esi)

	movl	8(%ebp),%eax	/* udaddr */
	subl	$4,%esi
	movl	%eax,(%esi)
	movl	PCPU(COPYOUT_BUF),%eax
	subl	$4,%esi
	movl	%eax,(%esi)
	movl	16(%ebp),%eax	/* len */
	subl	$4,%esi
	movl	%eax,(%esi)

	movl	%esp,%eax
	movl	%esi,%esp
	movl	%edi,%cr3

	/* bcopy(%esi = udaddr, %edi = PCPU(copyout_buf), %ecx = len) */
	popl	%ecx
	popl	%edi
	popl	%esi
	rep; movsb

	movl	%ebx,%cr3

	/* bcopy(%esi = PCPU(copyout_buf), %edi = kaddr, %ecx = len) */
	popl	%ecx
	popl	%edi
	popl	%esi
	rep; movsb

	movl	%eax,%esp
	sti

	xorl	%eax,%eax
	popl	%ebx
	popl	%edi
	popl	%esi
	leave
	ret
END(copyin_fast)

	ALIGN_TEXT
copyout_fault:
	movl	%eax,%esp
	sti
	movl	$EFAULT,%eax
	popl	%ebx
	popl	%edi
	popl	%esi
	leave
	ret

ENTRY(fueword_fast)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	8(%ebp),%ecx			/* from */
	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax),%eax
	movl	$fusufault,%edx
	movl	16(%ebp),%ebx
	movl	%esp,%esi
	cli
	movl	PCPU(TRAMPSTK),%esp
	movl	%eax,%cr3
	movl	(%ecx),%eax
	movl	%ebx,%cr3
	movl	%esi,%esp
	sti
	movl	12(%ebp),%edx
	movl	%eax,(%edx)
	xorl	%eax,%eax
	popl	%edi
	popl	%esi
	popl	%ebx
	leave
	ret
END(fueword_fast)

ENTRY(fuword16_fast)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	8(%ebp),%ecx			/* from */
	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax),%eax
	movl	$fusufault,%edx
	movl	12(%ebp),%ebx
	movl	%esp,%esi
	cli
	movl	PCPU(TRAMPSTK),%esp
	movl	%eax,%cr3
	movzwl	(%ecx),%eax
	movl	%ebx,%cr3
	movl	%esi,%esp
	sti
	popl	%edi
	popl	%esi
	popl	%ebx
	leave
	ret
END(fuword16_fast)

ENTRY(fubyte_fast)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	8(%ebp),%ecx			/* from */
	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax),%eax
	movl	$fusufault,%edx
	movl	12(%ebp),%ebx
	movl	%esp,%esi
	cli
	movl	PCPU(TRAMPSTK),%esp
	movl	%eax,%cr3
	movzbl	(%ecx),%eax
	movl	%ebx,%cr3
	movl	%esi,%esp
	sti
	popl	%edi
	popl	%esi
	popl	%ebx
	leave
	ret
END(fubyte_fast)

	ALIGN_TEXT
fusufault:
	movl	%esi,%esp
	sti
	xorl	%eax,%eax
	decl	%eax
	popl	%edi
	popl	%esi
	popl	%ebx
	leave
	ret

ENTRY(suword_fast)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax),%eax
	movl	$fusufault,%edx
	movl	8(%ebp),%ecx			/* to */
	movl	12(%ebp),%edi			/* val */
	movl	16(%ebp),%ebx
	movl	%esp,%esi
	cli
	movl	PCPU(TRAMPSTK),%esp
	movl	%eax,%cr3
	movl	%edi,(%ecx)
	movl	%ebx,%cr3
	movl	%esi,%esp
	sti
	xorl	%eax,%eax
	popl	%edi
	popl	%esi
	popl	%ebx
	leave
	ret
END(suword_fast)

ENTRY(suword16_fast)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax),%eax
	movl	$fusufault,%edx
	movl	8(%ebp),%ecx			/* to */
	movl	12(%ebp),%edi			/* val */
	movl	16(%ebp),%ebx
	movl	%esp,%esi
	cli
	movl	PCPU(TRAMPSTK),%esp
	movl	%eax,%cr3
	movw	%di,(%ecx)
	movl	%ebx,%cr3
	movl	%esi,%esp
	sti
	xorl	%eax,%eax
	popl	%edi
	popl	%esi
	popl	%ebx
	leave
	ret
END(suword16_fast)

ENTRY(subyte_fast)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	PCPU(CURPCB),%eax
	movl	PCB_CR3(%eax),%eax
	movl	$fusufault,%edx
	movl	8(%ebp),%ecx			/* to */
	movl	12(%ebp),%edi			/* val */
	movl	16(%ebp),%ebx
	movl	%esp,%esi
	cli
	movl	PCPU(TRAMPSTK),%esp
	movl	%eax,%cr3
	movl	%edi,%eax
	movb	%al,(%ecx)
	movl	%ebx,%cr3
	movl	%esi,%esp
	sti
	xorl	%eax,%eax
	popl	%edi
	popl	%esi
	popl	%ebx
	leave
	ret
END(subyte_fast)
