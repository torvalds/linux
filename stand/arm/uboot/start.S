/*-
 * Copyright (c) 2008 Semihalf, Rafal Czubak
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

#include <machine/asm.h>
#include <machine/armreg.h>

	.text
	.extern	_C_LABEL(self_reloc), _C_LABEL(main)
	.weak	_DYNAMIC

/*
 * Entry point to the loader that U-Boot passes control to.
 */
	.globl	_start
_start:

#ifdef _ARM_ARCH_6
	mrc     p15, 0, ip, c1, c0, 0
	orr	ip, ip, #(CPU_CONTROL_UNAL_ENABLE)
	orr	ip, ip, #(CPU_CONTROL_AFLT_ENABLE)
	mcr     p15, 0, ip, c1, c0, 0
#endif

	/* Save the arguments and return register before calling self_reloc */
	push	{r0, r1, r9, lr}

	/* 
	 * Do self-relocation when the weak external symbol _DYNAMIC is non-NULL.
	 * When linked as a dynamic relocatable file, the linker automatically
	 * defines _DYNAMIC with a value that is the offset of the dynamic
	 * relocation info section.
	 * Note that we're still on u-boot's stack here, but the self_reloc 
	 * code uses only a couple dozen bytes of stack space.
	 */
	adr	ip, .here_off		/* .here_off is a symbol whose value */
	ldr	r0, [ip]		/* is its own offset in the text seg. */
	sub	r0, ip, r0		/* Get its pc-relative address and */
	ldr	r1, .dynamic_off	/* subtract its value and we get */
	teq	r1, #0			/* r0 = physaddr we were loaded at. */
	addne	r1, r1, r0		/* r1 = dynamic section physaddr. */
	blne	_C_LABEL(self_reloc)	/* Do reloc if _DYNAMIC is non-NULL. */

	/* Restore saved arguments */
	pop	{r0, r1, r9, lr}

	/* Hint where to look for the API signature */
	ldr	ip, =uboot_address
	str	sp, [ip]

	/* Save U-Boot's r8 and r9 for syscall trampoline */
	ldr	ip, =saved_regs
	str	r8, [ip, #0]		/* old gd pointer (use to hold lr) */
	str	r9, [ip, #4]		/* new gd pointer */

	/* 
	 * Start loader.  Save return address first (r8 is available from
	 * trampoline save).
	 */
	mov	r8, lr
	bl	main
	mov	lr, r8

	/* Restore U-Boot environment */
	ldr	ip, =saved_regs
	ldr	r8, [ip, #0]
	ldr	r9, [ip, #4]
	mov	pc, lr

	/* 
	 * Data for self-relocation, in the text segment for pc-rel access.
	 */
.here_off:
	.word	.
.dynamic_off:
	.word	_DYNAMIC

/*
 * syscall()
 */
ENTRY(syscall)
	/* Save caller's lr, r8 and r9 */
	ldr	ip, =saved_regs
	str	r8, [ip, #8]
	str	r9, [ip, #12]
	str	lr, [ip, #16]
	/* Restore U-Boot's r8 and r9 */
	ldr	r8, [ip, #0]
	ldr	r9, [ip, #4]
	/* Call into U-Boot */
	ldr	lr, =return_from_syscall
	ldr	ip, =syscall_ptr
	ldr	pc, [ip]
return_from_syscall:
	/* Restore loader's r8, r9 and lr */
	ldr	ip, =saved_regs
	ldr	lr, [ip, #16]
	ldr	r9, [ip, #12]
	ldr	r8, [ip, #8]
	/* Return to caller */
	mov	pc, lr

/*
 * Data section
 */
	.data
	.align	4
	.globl	syscall_ptr
syscall_ptr:
	.long	0

	.globl	uboot_address
uboot_address:
	.long	0

saved_regs:
	.long	0	/* U-Boot's r8 */
	.long	0	/* U-Boot's r9 */
	.long	0	/* Loader's r8 */
	.long	0	/* Loader's r9 */
	.long	0	/* Loader's lr */
