/*	$NetBSD: cerror.S,v 1.14 2009/12/14 01:07:42 matt Exp $	*/

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

#include <machine/asm.h>
__FBSDID("$FreeBSD$");
#include "SYS.h"

#if defined(LIBC_SCCS) && !defined(lint)
#if 0
	RCSID("from: @(#)cerror.s	8.1 (Berkeley) 6/16/93")
#else
	RCSID("$NetBSD: cerror.S,v 1.14 2009/12/14 01:07:42 matt Exp $")
#endif
#endif /* LIBC_SCCS and not lint */

	.globl	_C_LABEL(__error)
NESTED_NOPROFILE(__cerror, CALLFRAME_SIZ, ra)
	.mask	0x80000000, (CALLFRAME_RA - CALLFRAME_SIZ)
	SETUP_GP
	PTR_SUBU	sp, sp, CALLFRAME_SIZ
	SETUP_GP64(CALLFRAME_GP, __cerror)
	SAVE_GP(CALLFRAME_GP)

	PTR_S		ra, CALLFRAME_RA(sp)
	REG_S		v0, CALLFRAME_S0(sp)	# save errno value

	PTR_LA		t9, _C_LABEL(__error)	# locate address of errno
	jalr		t9

	REG_L		t0, CALLFRAME_S0(sp)
	PTR_L		ra, CALLFRAME_RA(sp)
	INT_S		t0, 0(v0)		# update errno value

	RESTORE_GP64
	PTR_ADDU	sp, sp, CALLFRAME_SIZ

	li		v0, -1
	li		v1, -1

	j		ra
END(__cerror)
