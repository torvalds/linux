/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994
 *      The Regents of the University of California.  All rights reserved.
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
 *	@(#)mntopts.h	8.7 (Berkeley) 3/29/95
 * $FreeBSD$
 */

struct mntopt {
	const char *m_option;	/* option name */
	int m_inverse;		/* if a negative option, e.g. "atime" */
	long long m_flag;	/* bit to set, e.g. MNT_RDONLY */
	int m_altloc;		/* 1 => set bit in altflags */
};

/* User-visible MNT_ flags. */
#define MOPT_ASYNC		{ "async",	0, MNT_ASYNC, 0 }
#define MOPT_NOATIME		{ "atime",	1, MNT_NOATIME, 0 }
#define MOPT_NOEXEC		{ "exec",	1, MNT_NOEXEC, 0 }
#define MOPT_NOSUID		{ "suid",	1, MNT_NOSUID, 0 }
#define MOPT_NOSYMFOLLOW	{ "symfollow",  1, MNT_NOSYMFOLLOW, 0 }
#define MOPT_RDONLY		{ "rdonly",	0, MNT_RDONLY, 0 }
#define MOPT_SYNC		{ "sync",	0, MNT_SYNCHRONOUS, 0 }
#define MOPT_UNION		{ "union",	0, MNT_UNION, 0 }
#define MOPT_USERQUOTA		{ "userquota",	0, 0, 0 }
#define MOPT_GROUPQUOTA		{ "groupquota",	0, 0, 0 }
#define MOPT_NOCLUSTERR		{ "clusterr",	1, MNT_NOCLUSTERR, 0 }
#define MOPT_NOCLUSTERW		{ "clusterw",	1, MNT_NOCLUSTERW, 0 }
#define MOPT_SUIDDIR		{ "suiddir",	0, MNT_SUIDDIR, 0 }
#define MOPT_SNAPSHOT		{ "snapshot",	0, MNT_SNAPSHOT, 0 }
#define MOPT_MULTILABEL		{ "multilabel",	0, MNT_MULTILABEL, 0 }
#define MOPT_ACLS		{ "acls",	0, MNT_ACLS, 0 }
#define MOPT_NFS4ACLS		{ "nfsv4acls",	0, MNT_NFS4ACLS, 0 }
#define MOPT_AUTOMOUNTED	{ "automounted",0, MNT_AUTOMOUNTED, 0 }

/* Control flags. */
#define MOPT_FORCE		{ "force",	0, MNT_FORCE, 0 }
#define MOPT_UPDATE		{ "update",	0, MNT_UPDATE, 0 }
#define MOPT_RO			{ "ro",		0, MNT_RDONLY, 0 }
#define MOPT_RW			{ "rw",		1, MNT_RDONLY, 0 }

/* This is parsed by mount(8), but is ignored by specific mount_*(8)s. */
#define MOPT_AUTO		{ "auto",	0, 0, 0 }

/* A handy macro as terminator of MNT_ array. */
#define MOPT_END		{ NULL,		0, 0, 0 }

#define MOPT_FSTAB_COMPAT						\
	MOPT_RO,							\
	MOPT_RW,							\
	MOPT_AUTO

/* Standard options which all mounts can understand. */
#define MOPT_STDOPTS							\
	MOPT_USERQUOTA,							\
	MOPT_GROUPQUOTA,						\
	MOPT_FSTAB_COMPAT,						\
	MOPT_NOATIME,							\
	MOPT_NOEXEC,							\
	MOPT_SUIDDIR,		/* must be before MOPT_NOSUID */	\
	MOPT_NOSUID,							\
	MOPT_NOSYMFOLLOW,						\
	MOPT_RDONLY,							\
	MOPT_UNION,							\
	MOPT_NOCLUSTERR,						\
	MOPT_NOCLUSTERW,						\
	MOPT_MULTILABEL,						\
	MOPT_ACLS,							\
	MOPT_NFS4ACLS,							\
	MOPT_AUTOMOUNTED

void getmntopts(const char *, const struct mntopt *, int *, int *);
void rmslashes(char *, char *);
int checkpath(const char *, char resolved_path[]);
extern int getmnt_silent;
void build_iovec(struct iovec **iov, int *iovlen, const char *name, void *val, size_t len);
void build_iovec_argf(struct iovec **iov, int *iovlen, const char *name, const char *fmt, ...);
void free_iovec(struct iovec **iovec, int *iovlen);
