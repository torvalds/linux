/*	$OpenBSD: _lock.h,v 1.7 2025/04/14 12:53:54 visa Exp $	*/

/*-
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/sys/_lock.h 179025 2008-05-15 20:10:06Z attilio $
 */

#ifndef _SYS__LOCK_H_
#define	_SYS__LOCK_H_

#define	LO_CLASSFLAGS	0x0000ffff	/* Class specific flags. */
#define	LO_INITIALIZED	0x00010000	/* Lock has been initialized. */
#define	LO_WITNESS	0x00020000	/* Should witness monitor this lock. */
#define	LO_RECURSABLE	0x00080000	/* Lock may recurse. */
#define	LO_SLEEPABLE	0x00100000	/* Lock may be held while sleeping. */
#define	LO_UPGRADABLE	0x00200000	/* Lock may be upgraded/downgraded. */
#define	LO_DUPOK	0x00400000	/* Don't check for duplicate acquires */
#define	LO_IS_VNODE	0x00800000	/* Tell WITNESS about a VNODE lock */
#define	LO_CLASSMASK	0x0f000000	/* Class index bitmask. */
#define	LO_HASPARENT	0x40000000	/* If set, non-NULL lo_relative
					 * points to parent lock.
					 * If clear, non-NULL lo_relative
					 * points to child lock. */

#define	LO_CLASSSHIFT		24

enum lock_class_index {
	LO_CLASS_KERNEL_LOCK,
	LO_CLASS_MUTEX,
	LO_CLASS_RWLOCK,
	LO_CLASS_RRWLOCK
};

struct lock_object {
	const struct lock_type	*lo_type;
	const char		*lo_name;	/* Individual lock name. */
	struct witness		*lo_witness;	/* Data for witness. */
	struct lock_object	*lo_relative;	/* Parent or child lock. */
	unsigned int		 lo_flags;
};

struct lock_type {
	const char		*lt_name;
};

#endif /* !_SYS__LOCK_H_ */
