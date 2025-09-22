/*	$OpenBSD: lock.h,v 1.27 2016/06/19 11:54:33 natano Exp $	*/

/* 
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code contains ideas from software contributed to Berkeley by
 * Avadis Tevanian, Jr., Michael Wayne Young, and the Mach Operating
 * System project at Carnegie-Mellon University.
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
 *	@(#)lock.h	8.12 (Berkeley) 5/19/95
 */

#ifndef	_LOCK_H_
#define	_LOCK_H_

#include <sys/rwlock.h>

#define LK_EXCLUSIVE	RW_WRITE	/* exclusive lock */
#define LK_SHARED	RW_READ		/* shared lock */
#define LK_TYPE_MASK	(RW_WRITE|RW_READ) /* type of lock sought */
#define LK_NOWAIT	RW_NOSLEEP	/* do not sleep to await lock */
#define LK_RECURSEFAIL	RW_RECURSEFAIL	/* fail if recursive exclusive lock */
#define LK_EXCLOTHER	RW_WRITE_OTHER	/* exclusive lock held by some other thread */
#define LK_RWFLAGS	(RW_WRITE|RW_READ|RW_NOSLEEP|RW_RECURSEFAIL|RW_WRITE_OTHER)

/* LK_ specific */
#define LK_DRAIN	0x1000UL	/* wait for all lock activity to end */
#define LK_RETRY	0x2000UL	/* vn_lock: retry until locked */

#endif /* !_LOCK_H_ */
