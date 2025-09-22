/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)config.h	8.1 (Berkeley) 6/6/93
 *	$Id: config.h,v 1.12 2015/12/11 04:26:01 mmcc Exp $
 */

/*
 * Pick up target dependent definitions
 */
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>

#define clocktime() (clock_valid ? clock_valid : time(&clock_valid))
extern time_t clock_valid;	/* Clock needs recalculating */

extern char *__progname;
extern char hostname[];		/* "kiska" */
extern pid_t mypid;		/* Current process id */

extern int syslogging;		/* Really using syslog */
extern FILE *logfp;		/* Log file */
extern int xlog_level;		/* Logging level */
extern int xlog_level_init;

extern int orig_umask;		/* umask() on startup */

#define	XLOG_FATAL	0x0001
#define	XLOG_ERROR	0x0002
#define	XLOG_USER	0x0004
#define	XLOG_WARNING	0x0008
#define	XLOG_INFO	0x0010
#define	XLOG_DEBUG	0x0020
#define	XLOG_MAP	0x0040
#define	XLOG_STATS	0x0080

#define XLOG_DEFSTR	"all,nomap,nostats"		/* Default log options */
#define XLOG_ALL	(XLOG_FATAL|XLOG_ERROR|XLOG_USER|XLOG_WARNING|XLOG_INFO|XLOG_MAP|XLOG_STATS)

#ifdef DEBUG
#define	D_ALL	(~0)

#define Debug(x) if (!(debug_flags & (x))) ; else
#define dlog Debug(D_FULL) dplog
#endif /* DEBUG */

/*
 * Option tables
 */
struct opt_tab {
	char *opt;
	int flag;
};

extern struct opt_tab xlog_opt[];

extern int cmdoption(char *, struct opt_tab *, int *);
extern void going_down(int);
#ifdef DEBUG
#define dplog(fmt, args...) plog(XLOG_DEBUG, fmt, ## args)
#endif /* DEBUG */
extern void plog(int, const char *, ...)
    __attribute__((__format__ (syslog, 2, 3)));
extern void show_opts(int ch, struct opt_tab *);
__dead void xmallocfailure(void);
extern void *xmalloc(size_t);
extern void *xreallocarray(void *, size_t, size_t);
