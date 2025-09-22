/*	$OpenBSD: mntopts.h,v 1.18 2016/09/10 16:53:30 natano Exp $	*/
/*	$NetBSD: mntopts.h,v 1.3 1995/03/18 14:56:59 cgd Exp $	*/

/*-
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
 *	@(#)mntopts.h	8.3 (Berkeley) 3/27/94
 */

#define MFLAG_INVERSE		0x01	/* if a negative option, eg "dev" */
#define MFLAG_SET		0x02	/* 1 => set bit in mntflags,
					   0 => return flag */
#define MFLAG_STRVAL		0x04	/* option needs a string value */
#define MFLAG_INTVAL		0x08	/* option needs an int value */
#define MFLAG_OPT		0x10	/* value is optional */

struct mntopt {
	const char *m_option;	/* option name */
	int m_flag;		/* bit to set, eg. MNT_RDONLY */
	int m_oflags;
};

union mntval {
	char *strval;
	int ival;
};

/* User-visible MNT_ flags. */
#define MOPT_ASYNC	{ "async",	MNT_ASYNC, MFLAG_SET }
#define MOPT_NOACCESSTIME	{ "accesstime", MNT_NOATIME,		\
					MFLAG_INVERSE | MFLAG_SET }
#define MOPT_NOATIME	{ "atime",      MNT_NOATIME, MFLAG_INVERSE | MFLAG_SET }
#define MOPT_NODEV	{ "dev",	MNT_NODEV, MFLAG_INVERSE | MFLAG_SET }
#define MOPT_NOEXEC	{ "exec",	MNT_NOEXEC, MFLAG_INVERSE | MFLAG_SET }
#define MOPT_NOSUID	{ "suid",	MNT_NOSUID, MFLAG_INVERSE | MFLAG_SET }
#define MOPT_NOPERM	{ "perm",	MNT_NOPERM, MFLAG_INVERSE | MFLAG_SET }
#define MOPT_WXALLOWED	{ "wxallowed",	MNT_WXALLOWED, MFLAG_SET }
#define MOPT_RDONLY	{ "rdonly",	MNT_RDONLY, MFLAG_SET }
#define MOPT_SYNC	{ "sync",	MNT_SYNCHRONOUS, MFLAG_SET }
#define MOPT_USERQUOTA	{ "userquota",	0, MFLAG_SET | MFLAG_STRVAL \
					    | MFLAG_OPT }
#define MOPT_GROUPQUOTA	{ "groupquota",	0, MFLAG_SET | MFLAG_STRVAL \
					    | MFLAG_OPT }
#define MOPT_SOFTDEP	{ "softdep",	MNT_SOFTDEP, MFLAG_SET }

/* Control flags. */
#define MOPT_FORCE	{ "force",	MNT_FORCE, MFLAG_SET }
#define MOPT_UPDATE	{ "update",	MNT_UPDATE, MFLAG_SET }
#define MOPT_RELOAD	{ "reload",	MNT_RELOAD, MFLAG_SET }

/* Support for old-style "ro", "rw" flags. */
#define MOPT_RO		{ "ro",		MNT_RDONLY, MFLAG_SET }
#define MOPT_RW		{ "rw",		MNT_RDONLY, MFLAG_INVERSE | MFLAG_SET }

/* These are parsed by mount(8), but are ignored by specific mount_*(8)s. */
#define MOPT_AUTO		{ "auto",	0, MFLAG_SET }
#define MOPT_NET		{ "net",	0, MFLAG_SET }

#define MOPT_FSTAB_COMPAT						\
	MOPT_RO,							\
	MOPT_RW,							\
	MOPT_NET,							\
	MOPT_AUTO

/* Standard options which all mounts can understand. */
#define MOPT_STDOPTS							\
	MOPT_USERQUOTA,							\
	MOPT_GROUPQUOTA,						\
	MOPT_FSTAB_COMPAT,						\
	MOPT_NOACCESSTIME,						\
	MOPT_NOATIME,							\
	MOPT_NODEV,							\
	MOPT_NOEXEC,							\
	MOPT_NOSUID,							\
	MOPT_RDONLY

int getmntopts(const char *, const struct mntopt *, int *);
int getmntopt(char **, union mntval *, const struct mntopt *, int *);
