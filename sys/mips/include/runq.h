/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder <jake@FreeBSD.org>
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
 *	from: src/sys/i386/include/runq.h,v 1.3 2005/01/06 22:18:15 imp
 * $FreeBSD$
 */

#ifndef _MACHINE_RUNQ_H_
#define	_MACHINE_RUNQ_H_

#ifdef __mips_n64
#define	RQB_LEN		(1)		/* Number of priority status words. */
#define	RQB_L2BPW	(6)		/* Log2(sizeof(rqb_word_t) * NBBY)). */
#else
#define	RQB_LEN		(2)		/* Number of priority status words. */
#define	RQB_L2BPW	(5)		/* Log2(sizeof(rqb_word_t) * NBBY)). */
#endif
#define	RQB_BPW		(1<<RQB_L2BPW)	/* Bits in an rqb_word_t. */

#define	RQB_BIT(pri)	(1ul << ((pri) & (RQB_BPW - 1)))
#define	RQB_WORD(pri)	((pri) >> RQB_L2BPW)

#ifdef __mips_n64
#define	RQB_FFS(word)	(ffsl(word) - 1)
#else
#define	RQB_FFS(word)	(ffs(word) - 1)
#endif

/*
 * Type of run queue status word.
 */
#ifdef __mips_n64
typedef	u_int64_t	rqb_word_t;
#else
typedef	u_int32_t	rqb_word_t;
#endif

#endif
