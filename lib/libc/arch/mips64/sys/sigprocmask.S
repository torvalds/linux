/*	$OpenBSD: sigprocmask.S,v 1.8 2023/12/10 16:45:51 deraadt Exp $ */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
 */

#include "SYS.h"

FRAMESZ= MKFSIZ(1,1)
GPOFF= FRAMESZ-2*REGSZ

SYSLEAF_HIDDEN(sigprocmask, FRAMESZ)
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, _HIDDEN(sigprocmask))
	.set	reorder
	bne	a1, zero, .Lgotptr	# if new sigset pointer not null
	li	a0, 1			# how = SIG_BLOCK
	b	.Ldoit			# mask = zero
.Lgotptr:
	lw	a1, 0(a1)		# indirect to new mask arg
.Ldoit:
	li	v0, SYS_sigprocmask
99:	syscall
	PINSYSCALL(SYS_sigprocmask, 99b)
	bne	a3, zero, .Lerr
	beq	a2, zero, .Lout		# test if old mask requested
	sw	v0, 0(a2)		# store old mask
.Lout:
	move	v0, zero
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	j	ra
.Lerr:
	LA	t9, CERROR
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	jr	t9
SYSCALL_END_HIDDEN(sigprocmask)
