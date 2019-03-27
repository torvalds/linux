/*
 * Copyright (c) 2001 Daniel Eischen <deischen@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * _ctx_start((void *func)(int arg1, ..., argn),
 *            int arg1, ..., argn, ucontext_t *ucp)
 *
 * 0(%esp)		- func
 * 4(%esp)		- arg1
 * 8(%esp)		- arg2
 * ...
 * (4*n)(%esp)		- argn
 * (4*(n + 1))(%esp)	- ucp, %ebp setup to point here (base of stack)
 */
ENTRY(_ctx_start)
	popl	%eax		/* get start function */
	call	*%eax		/* call start function */
	PIC_PROLOGUE
	movl	%esi, %esp	/*
				 * setup stack for completion routine;
				 * ucp is now at top of stack
				 */
	call	PIC_PLT(_ctx_done)	/* should never return */
	call	PIC_PLT(abort)		/* fubar */
	ret
END(_ctx_start)

	.section .note.GNU-stack,"",%progbits
