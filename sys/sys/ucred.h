/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)ucred.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _SYS_UCRED_H_
#define	_SYS_UCRED_H_

#include <bsm/audit.h>

struct loginclass;

#define	XU_NGROUPS	16

/*
 * Credentials.
 *
 * Please do not inspect cr_uid directly to determine superuserness.  The
 * priv(9) interface should be used to check for privilege.
 */
#if defined(_KERNEL) || defined(_WANT_UCRED)
struct ucred {
	u_int	cr_ref;			/* reference count */
#define	cr_startcopy cr_uid
	uid_t	cr_uid;			/* effective user id */
	uid_t	cr_ruid;		/* real user id */
	uid_t	cr_svuid;		/* saved user id */
	int	cr_ngroups;		/* number of groups */
	gid_t	cr_rgid;		/* real group id */
	gid_t	cr_svgid;		/* saved group id */
	struct uidinfo	*cr_uidinfo;	/* per euid resource consumption */
	struct uidinfo	*cr_ruidinfo;	/* per ruid resource consumption */
	struct prison	*cr_prison;	/* jail(2) */
	struct loginclass	*cr_loginclass; /* login class */
	u_int		cr_flags;	/* credential flags */
	void 		*cr_pspare2[2];	/* general use 2 */
#define	cr_endcopy	cr_label
	struct label	*cr_label;	/* MAC label */
	struct auditinfo_addr	cr_audit;	/* Audit properties. */
	gid_t	*cr_groups;		/* groups */
	int	cr_agroups;		/* Available groups */
	gid_t   cr_smallgroups[XU_NGROUPS];	/* storage for small groups */
};
#define	NOCRED	((struct ucred *)0)	/* no credential available */
#define	FSCRED	((struct ucred *)-1)	/* filesystem credential */
#endif /* _KERNEL || _WANT_UCRED */

/*
 * Flags for cr_flags.
 */
#define	CRED_FLAG_CAPMODE	0x00000001	/* In capability mode. */

/*
 * This is the external representation of struct ucred.
 */
struct xucred {
	u_int	cr_version;		/* structure layout version */
	uid_t	cr_uid;			/* effective user id */
	short	cr_ngroups;		/* number of groups */
	gid_t	cr_groups[XU_NGROUPS];	/* groups */
	void	*_cr_unused1;		/* compatibility with old ucred */
};
#define	XUCRED_VERSION	0

/* This can be used for both ucred and xucred structures. */
#define	cr_gid cr_groups[0]

#ifdef _KERNEL
struct proc;
struct thread;

void	change_egid(struct ucred *newcred, gid_t egid);
void	change_euid(struct ucred *newcred, struct uidinfo *euip);
void	change_rgid(struct ucred *newcred, gid_t rgid);
void	change_ruid(struct ucred *newcred, struct uidinfo *ruip);
void	change_svgid(struct ucred *newcred, gid_t svgid);
void	change_svuid(struct ucred *newcred, uid_t svuid);
void	crcopy(struct ucred *dest, struct ucred *src);
struct ucred	*crcopysafe(struct proc *p, struct ucred *cr);
struct ucred	*crdup(struct ucred *cr);
void	crextend(struct ucred *cr, int n);
void	proc_set_cred_init(struct proc *p, struct ucred *cr);
struct ucred	*proc_set_cred(struct proc *p, struct ucred *cr);
void	crfree(struct ucred *cr);
struct ucred	*crget(void);
struct ucred	*crhold(struct ucred *cr);
void	cru2x(struct ucred *cr, struct xucred *xcr);
void	crsetgroups(struct ucred *cr, int n, gid_t *groups);
int	groupmember(gid_t gid, struct ucred *cred);
#endif /* _KERNEL */

#endif /* !_SYS_UCRED_H_ */
