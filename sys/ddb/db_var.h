/*	$OpenBSD: db_var.h,v 1.15 2025/05/19 21:48:28 kettenis Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Different parameters/structures/functions
 */

#ifndef _DDB_DB_VAR_H_
#define _DDB_DB_VAR_H_

#ifndef	DB_HISTORY_SIZE
#define	DB_HISTORY_SIZE	4000
#endif

#define DBCTL_RADIX	1
#define DBCTL_MAXWIDTH	2
#define DBCTL_MAXLINE	3
#define DBCTL_TABSTOP	4
#define DBCTL_PANIC	5
#define DBCTL_CONSOLE	6
#define DBCTL_LOG	7
#define DBCTL_TRIGGER	8
#define DBCTL_PROFILE	9
#define DBCTL_SUSPEND	10
#define DBCTL_MAXID	11

#define	CTL_DDB_NAMES { \
	{ NULL, 0 }, \
	{ "radix", CTLTYPE_INT }, \
	{ "max_width", CTLTYPE_INT }, \
	{ "max_line", CTLTYPE_INT }, \
	{ "tab_stop_width", CTLTYPE_INT },\
	{ "panic", CTLTYPE_INT }, \
	{ "console", CTLTYPE_INT }, \
	{ "log", CTLTYPE_INT }, \
	{ "trigger", CTLTYPE_INT }, \
	{ "profile", CTLTYPE_INT }, \
	{ "suspend", CTLTYPE_INT }, \
}

#ifdef	_KERNEL
extern int	db_radix;
extern int	db_max_width;
extern int	db_tab_stop_width;
extern int	db_max_line;
extern int	db_panic;
extern int	db_console;
extern int	db_log;
extern int	db_profile;
extern int	db_suspend;

int	ddb_sysctl(int *, u_int, void *, size_t *, void *, size_t,
	    struct proc *);
#endif

#endif /* _DDB_DB_VAR_H_ */
