/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1989, 1992, 1993
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
 *	From: @(#)systat.h	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

#include <curses.h>

struct  cmdtab {
	const char *c_name;		/* command name */
	void	(*c_refresh)(void);	/* display refresh */
	void	(*c_fetch)(void);	/* sets up data structures */
	void	(*c_label)(void);	/* label display */
	int	(*c_init)(void);	/* initialize namelist, etc. */
	WINDOW	*(*c_open)(void);	/* open display */
	void	(*c_close)(WINDOW *);	/* close display */
	int	(*c_cmd)(const char *, const char *); /* display command interpreter */
	void	(*c_reset)(void);	/* reset ``mode since'' display */
	char	c_flags;		/* see below */
};

/*
 * If we are started with privileges, use a kmem interface for netstat handling,
 * otherwise use sysctl.
 * In case of many open sockets, the sysctl handling might become slow.
 */
extern int use_kvm;

#define	CF_INIT		0x1		/* been initialized */
#define	CF_LOADAV	0x2		/* display w/ load average */
#define	CF_ZFSARC	0x4		/* display w/ ZFS cache usage */

#define	TCP	0x1
#define	UDP	0x2

#define	MAINWIN_ROW	3		/* top row for the main/lower window */

#define GETSYSCTL(name, var) getsysctl(name, &(var), sizeof(var))
#define KREAD(addr, buf, len)  kvm_ckread((addr), (buf), (len))
#define NVAL(indx)  namelist[(indx)].n_value
#define NPTR(indx)  (void *)NVAL((indx))
#define NREAD(indx, buf, len) kvm_ckread(NPTR((indx)), (buf), (len))
