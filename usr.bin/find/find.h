/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Cimarron D. Taylor of the University of California, Berkeley.
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
 *	@(#)find.h	8.1 (Berkeley) 6/6/93
 *	$FreeBSD$
 */

#include <regex.h>
#include <sys/mount.h>
#include <sys/stat.h>

/*
 * We need to build find during the bootstrap stage when building on a
 * non-FreeBSD system. Linux does not have the st_flags and st_birthtime
 * members in struct stat so we need to omit support for tests that depend
 * on these members. This works fine since none of these flags are used
 * during the build of world and kernel.
 */
#ifdef UF_SETTABLE
#define HAVE_STRUCT_STAT_ST_FLAGS 1
#else
#define HAVE_STRUCT_STAT_ST_FLAGS 0
#endif
#if defined(st_birthtime) || defined(st_birthtimespec)
#define HAVE_STRUCT_STAT_ST_BIRTHTIME 1
#else
#define HAVE_STRUCT_STAT_ST_BIRTHTIME 0
#endif
#if defined(MFSNAMELEN) || defined(MFSTYPENAMELEN)
#define HAVE_STRUCT_STATFS_F_FSTYPENAME 1
#else
#define HAVE_STRUCT_STATFS_F_FSTYPENAME 0
#endif

/* forward declarations */
struct _plandata;
struct _option;

/* execute function */
typedef int exec_f(struct _plandata *, FTSENT *);
/* create function */
typedef	struct _plandata *creat_f(struct _option *, char ***);

/* function modifiers */
#define	F_NEEDOK	0x00000001	/* -ok vs. -exec */
#define	F_EXECDIR	0x00000002	/* -execdir vs. -exec */
#define F_TIME_A	0x00000004	/* one of -atime, -anewer, -newera* */
#define F_TIME_C	0x00000008	/* one of -ctime, -cnewer, -newerc* */
#define	F_TIME2_A	0x00000010	/* one of -newer?a */
#define	F_TIME2_C	0x00000020	/* one of -newer?c */
#define	F_TIME2_T	0x00000040	/* one of -newer?t */
#define F_MAXDEPTH	F_TIME_A	/* maxdepth vs. mindepth */
#define F_DEPTH		F_TIME_A	/* -depth n vs. -d */
/* command line function modifiers */
#define	F_EQUAL		0x00000000	/* [acm]min [acm]time inum links size */
#define	F_LESSTHAN	0x00000100
#define	F_GREATER	0x00000200
#define F_ELG_MASK	0x00000300
#define	F_ATLEAST	0x00000400	/* flags perm */
#define F_ANY		0x00000800	/* perm */
#define	F_MTMASK	0x00003000
#define	F_MTFLAG	0x00000000	/* fstype */
#define	F_MTTYPE	0x00001000
#define	F_MTUNKNOWN	0x00002000
#define	F_IGNCASE	0x00010000	/* iname ipath iregex */
#define	F_EXACTTIME	F_IGNCASE	/* -[acm]time units syntax */
#define F_EXECPLUS	0x00020000	/* -exec ... {} + */
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
#define	F_TIME_B	0x00040000	/* one of -Btime, -Bnewer, -newerB* */
#define	F_TIME2_B	0x00080000	/* one of -newer?B */
#endif
#define F_LINK		0x00100000	/* lname or ilname */

/* node definition */
typedef struct _plandata {
	struct _plandata *next;		/* next node */
	exec_f	*execute;		/* node evaluation function */
	int flags;			/* private flags */
	union {
		gid_t _g_data;		/* gid */
		ino_t _i_data;		/* inode */
		mode_t _m_data;		/* mode mask */
		struct {
			u_long _f_flags;
			u_long _f_notflags;
		} fl;
		nlink_t _l_data;		/* link count */
		short _d_data;			/* level depth (-1 to N) */
		off_t _o_data;			/* file size */
		struct timespec _t_data;	/* time value */
		uid_t _u_data;			/* uid */
		short _mt_data;			/* mount flags */
		struct _plandata *_p_data[2];	/* PLAN trees */
		struct _ex {
			char **_e_argv;		/* argv array */
			char **_e_orig;		/* original strings */
			int *_e_len;		/* allocated length */
			int _e_pbnum;		/* base num. of args. used */
			int _e_ppos;		/* number of arguments used */
			int _e_pnummax;		/* max. number of arguments */
			int _e_psize;		/* number of bytes of args. */
			int _e_pbsize;		/* base num. of bytes of args */
			int _e_psizemax;	/* max num. of bytes of args */
			struct _plandata *_e_next;/* next F_EXECPLUS in tree */
		} ex;
		char *_a_data[2];		/* array of char pointers */
		char *_c_data;			/* char pointer */
		regex_t *_re_data;		/* regex */
	} p_un;
} PLAN;
#define	a_data	p_un._a_data
#define	c_data	p_un._c_data
#define	d_data	p_un._d_data
#define fl_flags	p_un.fl._f_flags
#define fl_notflags	p_un.fl._f_notflags
#define	g_data	p_un._g_data
#define	i_data	p_un._i_data
#define	l_data	p_un._l_data
#define	m_data	p_un._m_data
#define	mt_data	p_un._mt_data
#define	o_data	p_un._o_data
#define	p_data	p_un._p_data
#define	t_data	p_un._t_data
#define	u_data	p_un._u_data
#define	re_data	p_un._re_data
#define	e_argv	p_un.ex._e_argv
#define	e_orig	p_un.ex._e_orig
#define	e_len	p_un.ex._e_len
#define e_pbnum	p_un.ex._e_pbnum
#define e_ppos	p_un.ex._e_ppos
#define e_pnummax p_un.ex._e_pnummax
#define e_psize p_un.ex._e_psize
#define e_pbsize p_un.ex._e_pbsize
#define e_psizemax p_un.ex._e_psizemax
#define e_next p_un.ex._e_next

typedef struct _option {
	const char *name;		/* option name */
	creat_f *create;		/* create function */
	exec_f *execute;		/* execute function */
	int flags;
} OPTION;

#include "extern.h"
