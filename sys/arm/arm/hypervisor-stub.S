/*
 * Copyright (C) 2015 Mihai Carabas <mihai.carabas@gmail.com>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "assym.inc"
#include <sys/syscall.h>
#include <machine/asm.h>
#include <machine/asmacros.h>
#include <machine/armreg.h>

__FBSDID("$FreeBSD$");

#if __ARM_ARCH >= 7
#if defined(__ARM_ARCH_7VE__) || defined(__clang__)
.arch_extension virt
#endif

ASENTRY_NP(hypervisor_stub_vect_install)

	/* Install hypervisor stub vectors. */
	adr	r0, hypervisor_stub_vect
	mcr	CP15_HVBAR(r0)

	/* Disable all the traps in the hypervisor. */
	mov	r0, #0
	mcr	CP15_HCR(r0)
	mcr	CP15_HCPTR(r0)
	mcr	CP15_HSTR(r0)
	mcr	CP15_HSCTLR(r0)

	/* Don't disable access to perf-mon from PL0,1 and preserve HPMN. */
	mrc	CP15_HDCR(r0)
	and	r0, #(ARM_CP15_HDCR_HPMN)
	/*  Caller implicit instruction barrier in the ERET. */
	mcr	CP15_HDCR(r0)

	RET

END(hypervisor_stub_vect_install)

ASENTRY_NP(hypervisor_stub_trap)
	/*
	 * If the first parameter is -1 than return the
	 * exception vector (HVBAR), otherwise set it to
	 * the value of it.
	 */
	cmp	r0, #-1
	mrceq	CP15_HVBAR(r0)
	mcrne	CP15_HVBAR(r0)
	ERET
END(hypervisor_stub_trap)

	.globl hypervisor_stub_vect
	.align 5
_C_LABEL(hypervisor_stub_vect):
	.word 0 /* Reset */
	.word 0 /* undev */
	.word 0 /* SMC */
	.word 0 /* PABT */
	.word 0 /* DABT */
	b	hypervisor_stub_trap /* HYP-Mode */
	.word 0 /* FIQ */
	.word 0 /* IRQ */
#endif /* __ARM_ARCH >= 7 */

