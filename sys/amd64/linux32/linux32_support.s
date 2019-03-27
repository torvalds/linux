/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Konstantin Belousov
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * $FreeBSD$
 */

#include "linux32_assym.h"		/* system definitions */
#include <machine/asmacros.h>		/* miscellaneous asm macros */

#include "assym.inc"

futex_fault:
	movq	$0,PCB_ONFAULT(%r8)
	movl	$-EFAULT,%eax
	ret

ENTRY(futex_xchgl_nosmap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	xchgl	%edi,(%rsi)
	movl	%edi,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_xchgl_nosmap)

ENTRY(futex_xchgl_smap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	stac
	xchgl	%edi,(%rsi)
	clac
	movl	%edi,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_xchgl_smap)

ENTRY(futex_addl_nosmap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
#ifdef SMP
	lock
#endif
	xaddl	%edi,(%rsi)
	movl	%edi,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_addl_nosmap)

ENTRY(futex_addl_smap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	stac
#ifdef SMP
	lock
#endif
	xaddl	%edi,(%rsi)
	clac
	movl	%edi,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_addl_smap)

ENTRY(futex_orl_nosmap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	movl	(%rsi),%eax
1:	movl	%eax,%ecx
	orl	%edi,%ecx
#ifdef SMP
	lock
#endif
	cmpxchgl %ecx,(%rsi)
	jnz	1b
	movl	%eax,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_orl_nosmap)

ENTRY(futex_orl_smap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	movl	(%rsi),%eax
1:	movl	%eax,%ecx
	orl	%edi,%ecx
	stac
#ifdef SMP
	lock
#endif
	cmpxchgl %ecx,(%rsi)
	clac
	jnz	1b
	movl	%eax,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_orl_smap)

ENTRY(futex_andl_nosmap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	movl	(%rsi),%eax
1:	movl	%eax,%ecx
	andl	%edi,%ecx
#ifdef SMP
	lock
#endif
	cmpxchgl %ecx,(%rsi)
	jnz	1b
	movl	%eax,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_andl_nosmap)

ENTRY(futex_andl_smap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	movl	(%rsi),%eax
1:	movl	%eax,%ecx
	andl	%edi,%ecx
	stac
#ifdef SMP
	lock
#endif
	cmpxchgl %ecx,(%rsi)
	clac
	jnz	1b
	movl	%eax,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_andl_smap)

ENTRY(futex_xorl_nosmap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	movl	(%rsi),%eax
1:	movl	%eax,%ecx
	xorl	%edi,%ecx
#ifdef SMP
	lock
#endif
	cmpxchgl %ecx,(%rsi)
	jnz	1b
	movl	%eax,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_xorl_nosmap)

ENTRY(futex_xorl_smap)
	movq	PCPU(CURPCB),%r8
	movq	$futex_fault,PCB_ONFAULT(%r8)
	movq	$VM_MAXUSER_ADDRESS-4,%rax
	cmpq	%rax,%rsi
	ja	futex_fault
	movl	(%rsi),%eax
1:	movl	%eax,%ecx
	xorl	%edi,%ecx
	stac
#ifdef SMP
	lock
#endif
	cmpxchgl %ecx,(%rsi)
	clac
	jnz	1b
	movl	%eax,(%rdx)
	xorl	%eax,%eax
	movq	%rax,PCB_ONFAULT(%r8)
	ret
END(futex_xorl_smap)
