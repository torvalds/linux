/*	$OpenBSD: spl.S,v 1.20 2023/07/27 00:30:07 guenther Exp $	*/
/*	$NetBSD: spl.S,v 1.3 2004/06/28 09:13:11 fvdl Exp $	*/

/*
 * Copyright (c) 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/segments.h>
#include <machine/frameasm.h>

#include "assym.h"

	.text

#if 0
#if defined(PROF) || defined(GPROF)
/*
 * XXXX TODO
 */
_ENTRY(splhigh)
	RETGUARD_SETUP(splhigh, r11)
	movl	$IPL_HIGH,%eax
	xchgl	%eax,CPUVAR(ILEVEL)
	RETGUARD_CHECK(splhigh, r11)
	ret
	lfence

_ENTRY(splx)
	RETGUARD_SETUP(splx, r11)
	movl	4(%esp),%eax
	movl	%eax,CPUVAR(ILEVEL)
	testl	%eax,%eax
	jnz	Xspllower
	RETGUARD_CHECK(splx, r11)
	ret
	lfence
#endif /* PROF || GPROF */
#endif

/*
 * Process pending interrupts.
 *
 * Important registers:
 *   ebx - cpl
 *   r13 - address to resume loop at
 *
 * It is important that the bit scan instruction is bsr, it will get
 * the highest 2 bits (currently the IPI and clock handlers) first,
 * to avoid deadlocks where one CPU sends an IPI, another one is at
 * splipi() and defers it, lands in here via splx(), and handles
 * a lower-prio one first, which needs to take the kernel lock -->
 * the sending CPU will never see the that CPU accept the IPI
 */
KIDTVEC(spllower)
	_PROF_PROLOGUE
	RETGUARD_SETUP(Xspllower, r11)
	pushq	%rbx
	pushq	%r13
	RETGUARD_PUSH(r11)
	movl	%edi,%ebx
	leaq	1f(%rip),%r13		# address to resume loop at
1:	endbr64
	movl	%ebx,%eax		# get cpl
	movq	CPUVAR(IUNMASK)(,%rax,8),%rax
	cli
	andq	CPUVAR(IPENDING),%rax		# any non-masked bits left?
	jnz	2f
	movl	%ebx,CPUVAR(ILEVEL)
	sti
	RETGUARD_POP(r11)
	popq	%r13
	popq	%rbx
	RETGUARD_CHECK(Xspllower, r11)
	ret
	lfence
2:	bsrq	%rax,%rax
	btrq	%rax,CPUVAR(IPENDING)
	movq	CPUVAR(ISOURCES)(,%rax,8),%rax
	movq    IS_RECURSE(%rax),%rax
	jmp     retpoline_rax
END(Xspllower)

/*
 * Handle return from interrupt after device handler finishes.
 *
 * Important registers:
 *   ebx - cpl to restore
 *   r13 - address to resume loop at
 */
KIDTVEC(doreti)
	movq	IF_PPL(%rsp),%rbx	# get previous priority
	decl	CPUVAR(IDEPTH)
	leaq	1f(%rip),%r13
1:	endbr64
	movl	%ebx,%eax
	movq	CPUVAR(IUNMASK)(,%rax,8),%rax
	cli
	andq	CPUVAR(IPENDING),%rax
	jnz	2f
	movl	%ebx,CPUVAR(ILEVEL)
	testb	$SEL_RPL,TF_CS(%rsp)
	jnz	intr_user_exit		# Check for ASTs on exit to user mode
	INTRFASTEXIT
2:	bsrq    %rax,%rax               # slow, but not worth optimizing
	btrq    %rax,CPUVAR(IPENDING)
	movq	CPUVAR(ISOURCES)(,%rax, 8),%rax
	movq    IS_RESUME(%rax),%rax
	jmp     retpoline_rax
END(Xdoreti)
