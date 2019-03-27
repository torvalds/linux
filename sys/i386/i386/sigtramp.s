/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)locore.s	7.3 (Berkeley) 5/13/91
 * $FreeBSD$
 *
 *		originally from: locore.s, by William F. Jolitz
 *
 *		Substantially rewritten by David Greenman, Rod Grimes,
 *			Bruce Evans, Wolfgang Solfrank, Poul-Henning Kamp
 *			and many others.
 */

#include <sys/syscall.h>
#include <machine/asmacros.h>
#include <machine/psl.h>

#include "assym.inc"

/*
 * Signal trampoline, copied to top of user stack
 */
NON_GPROF_ENTRY(sigcode)
	calll	*SIGF_HANDLER(%esp)
	leal	SIGF_UC(%esp),%eax	/* get ucontext */
	pushl	%eax
	testl	$PSL_VM,UC_EFLAGS(%eax)
	jne	1f
	mov	UC_GS(%eax),%gs		/* restore %gs */
1:
	movl	$SYS_sigreturn,%eax
	pushl	%eax			/* junk to fake return addr. */
	int	$0x80			/* enter kernel with args */
					/* on stack */
1:
	jmp	1b

#ifdef COMPAT_FREEBSD4
	ALIGN_TEXT
freebsd4_sigcode:
	calll	*SIGF_HANDLER(%esp)
	leal	SIGF_UC4(%esp),%eax	/* get ucontext */
	pushl	%eax
	testl	$PSL_VM,UC4_EFLAGS(%eax)
	jne	1f
	mov	UC4_GS(%eax),%gs	/* restore %gs */
1:
	movl	$344,%eax		/* 4.x SYS_sigreturn */
	pushl	%eax			/* junk to fake return addr. */
	int	$0x80			/* enter kernel with args */
					/* on stack */
1:
	jmp	1b
#endif

#ifdef COMPAT_43
	ALIGN_TEXT
osigcode:
	call	*SIGF_HANDLER(%esp)	/* call signal handler */
	lea	SIGF_SC(%esp),%eax	/* get sigcontext */
	pushl	%eax
	testl	$PSL_VM,SC_PS(%eax)
	jne	9f
	mov	SC_GS(%eax),%gs		/* restore %gs */
9:
	movl	$103,%eax		/* 3.x SYS_sigreturn */
	pushl	%eax			/* junk to fake return addr. */
	int	$0x80			/* enter kernel with args */
0:	jmp	0b

/*
 * Our lcall $7,$0 handler remains in user mode (ring 3), since lcalls
 * don't change the interrupt mask, so if this one went directly to the
 * kernel then there would be a window with interrupts enabled in kernel
 * mode, and all interrupt handlers would have to be almost as complicated
 * as the NMI handler to support this.
 *
 * Instead, convert the lcall to an int0x80 call.  The kernel does most
 * of the conversion by popping the lcall return values off the user
 * stack and returning to them instead of to here, except when the
 * conversion itself fails.  Adjusting the stack here is impossible for
 * vfork() and harder for other syscalls.
 */
	ALIGN_TEXT
lcall_tramp:
	int	$0x80
1:	jmp	1b

#endif /* COMPAT_43 */

	ALIGN_TEXT
esigcode:

	.data
	.globl	szsigcode
szsigcode:
	.long	esigcode-sigcode
#ifdef COMPAT_FREEBSD4
	.globl	szfreebsd4_sigcode
szfreebsd4_sigcode:
	.long	esigcode-freebsd4_sigcode
#endif
#ifdef COMPAT_43
	.globl	szosigcode
szosigcode:
	.long	esigcode-osigcode
	.globl	sz_lcall_tramp
sz_lcall_tramp:
	.long	esigcode-lcall_tramp
#endif
