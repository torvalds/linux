/*	$OpenBSD: ucred.h,v 1.14 2022/03/17 14:23:34 visa Exp $	*/
/*	$NetBSD: ucred.h,v 1.12 1995/06/01 22:44:50 jtc Exp $	*/

/*
 * Copyright (c) 1989, 1993
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
 *	@(#)ucred.h	8.2 (Berkeley) 1/4/94
 */

#ifndef _SYS_UCRED_H_
#define	_SYS_UCRED_H_

#include <sys/refcnt.h>
#include <sys/syslimits.h>

/*
 * Credentials.
 */
struct ucred {
	struct refcnt	cr_refcnt;	/* reference count */

/* The following fields are all copied by crset() */
#define	cr_startcopy	cr_uid
	uid_t	cr_uid;			/* effective user id */
	uid_t	cr_ruid;		/* Real user id. */
	uid_t	cr_svuid;		/* Saved effective user id. */
	gid_t	cr_gid;			/* effective group id */
	gid_t	cr_rgid;		/* Real group id. */
	gid_t	cr_svgid;		/* Saved effective group id. */
	short	cr_ngroups;		/* number of groups */
	gid_t	cr_groups[NGROUPS_MAX];	/* groups */
};
#define NOCRED ((struct ucred *)-1)	/* no credential available */
#define FSCRED ((struct ucred *)-2)	/* filesystem credential */

/*
 *  Userspace version, for use in syscalls arguments
 */
struct xucred {
	uid_t	cr_uid;			/* user id */
	gid_t	cr_gid;			/* group id */
	short	cr_ngroups;		/* number of groups */
	gid_t	cr_groups[NGROUPS_MAX];	/* groups */
};

#ifdef _KERNEL

int		crfromxucred(struct ucred *, const struct xucred *);
void		crset(struct ucred *, const struct ucred *);
struct ucred	*crcopy(struct ucred *cr);
struct ucred	*crdup(struct ucred *cr);
void		crfree(struct ucred *cr);
struct ucred	*crget(void);
struct ucred	*crhold(struct ucred *);
int		suser(struct proc *p);
int		suser_ucred(struct ucred *cred);

#endif /* _KERNEL */

#endif /* !_SYS_UCRED_H_ */
