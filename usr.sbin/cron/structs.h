/*	$OpenBSD: structs.h,v 1.10 2020/04/16 17:51:56 millert Exp $	*/

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>

struct passwd;

typedef	struct _entry {
	SLIST_ENTRY(_entry) entries;
	struct passwd	*pwd;
	char		**envp;
	char		*cmd;
	bitstr_t	bit_decl(minute, MINUTE_COUNT);
	bitstr_t	bit_decl(hour,   HOUR_COUNT);
	bitstr_t	bit_decl(dom,    DOM_COUNT);
	bitstr_t	bit_decl(month,  MONTH_COUNT);
	bitstr_t	bit_decl(dow,    DOW_COUNT);
	int		flags;
#define	MIN_STAR	0x01
#define	HR_STAR		0x02
#define	DOM_STAR	0x04
#define	DOW_STAR	0x08
#define	WHEN_REBOOT	0x10
#define	DONT_LOG	0x20
#define	MAIL_WHEN_ERR	0x40
#define	SINGLE_JOB	0x80
} entry;

			/* the crontab database will be a list of the
			 * following structure, one element per user
			 * plus one for the system.
			 *
			 * These are the crontabs.
			 */

typedef	struct _user {
	TAILQ_ENTRY(_user) entries;	/* links */
	char		*name;
	struct timespec	mtime;		/* last modtime of crontab */
	SLIST_HEAD(crontab_list, _entry) crontab;	/* this person's crontab */
} user;

typedef	struct _cron_db {
	TAILQ_HEAD(user_list, _user) users;
	struct timespec	mtime;		/* last modtime on spooldir */
} cron_db;

typedef struct _atjob {
	TAILQ_ENTRY(_atjob) entries;	/* links */
	uid_t		uid;		/* uid of the job */
	gid_t		gid;		/* gid of the job */
	int		queue;		/* name of the at queue */
	time_t		run_time;	/* time to run at job */
} atjob;

typedef struct _at_db {
	TAILQ_HEAD(atjob_list, _atjob) jobs;
	struct timespec	mtime;		/* last modtime on spooldir */
} at_db;
