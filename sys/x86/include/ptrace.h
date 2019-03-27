/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ptrace.h	8.1 (Berkeley) 6/11/93
 * $FreeBSD$
 */

#ifndef _MACHINE_PTRACE_H_
#define _MACHINE_PTRACE_H_

#define	__HAVE_PTRACE_MACHDEP

/*
 * On amd64 (PT_FIRSTMACH + 0) and (PT_FIRSTMACH + 1) are old values for
 * PT_GETXSTATE_OLD and PT_SETXSTATE_OLD.  They should not be (re)used.
 */

#ifdef __i386__
#define	PT_GETXMMREGS	(PT_FIRSTMACH + 0)
#define	PT_SETXMMREGS	(PT_FIRSTMACH + 1)
#endif
#ifdef _KERNEL
#define	PT_GETXSTATE_OLD (PT_FIRSTMACH + 2)
#define	PT_SETXSTATE_OLD (PT_FIRSTMACH + 3)
#endif
#define	PT_GETXSTATE_INFO (PT_FIRSTMACH + 4)
#define	PT_GETXSTATE	(PT_FIRSTMACH + 5)
#define	PT_SETXSTATE	(PT_FIRSTMACH + 6)
#define	PT_GETFSBASE	(PT_FIRSTMACH + 7)
#define	PT_SETFSBASE	(PT_FIRSTMACH + 8)
#define	PT_GETGSBASE	(PT_FIRSTMACH + 9)
#define	PT_SETGSBASE	(PT_FIRSTMACH + 10)

/* Argument structure for PT_GETXSTATE_INFO. */
struct ptrace_xstate_info {
	uint64_t	xsave_mask;
	uint32_t	xsave_len;
};

#endif
