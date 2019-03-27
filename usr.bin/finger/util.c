/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Tony Nardo of the Johns Hopkins University/Applied Physics Lab.
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
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)util.c	8.3 (Berkeley) 4/28/95";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <ctype.h>
#include <db.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmpx.h>
#include "finger.h"
#include "pathnames.h"

static void	 find_idle_and_ttywrite(WHERE *);
static void	 userinfo(PERSON *, struct passwd *);
static WHERE	*walloc(PERSON *);

int
match(struct passwd *pw, const char *user)
{
	char *p, *t;
	char name[1024];

	if (!strcasecmp(pw->pw_name, user))
		return(1);

	/*
	 * XXX
	 * Why do we skip asterisks!?!?
	 */
	(void)strncpy(p = tbuf, pw->pw_gecos, sizeof(tbuf));
	tbuf[sizeof(tbuf) - 1] = '\0';
	if (*p == '*')
		++p;

	/* Ampersands get replaced by the login name. */
	if ((p = strtok(p, ",")) == NULL)
		return(0);

	for (t = name; t < &name[sizeof(name) - 1] && (*t = *p) != '\0'; ++p) {
		if (*t == '&') { 
			(void)strncpy(t, pw->pw_name, 
			    sizeof(name) - (t - name));
			name[sizeof(name) - 1] = '\0';
			while (t < &name[sizeof(name) - 1] && *++t)
				continue;
		} else {
			++t;
		}
	}
	*t = '\0';
	for (t = name; (p = strtok(t, "\t ")) != NULL; t = NULL)
		if (!strcasecmp(p, user))
			return(1);
	return(0);
}

void
enter_lastlog(PERSON *pn)
{
	WHERE *w;
	struct utmpx *ut = NULL;
	char doit = 0;

	if (setutxdb(UTXDB_LASTLOGIN, NULL) == 0)
		ut = getutxuser(pn->name);
	if ((w = pn->whead) == NULL)
		doit = 1;
	else if (ut != NULL && ut->ut_type == USER_PROCESS) {
		/* if last login is earlier than some current login */
		for (; !doit && w != NULL; w = w->next)
			if (w->info == LOGGEDIN &&
			    w->loginat < ut->ut_tv.tv_sec)
				doit = 1;
		/*
		 * and if it's not any of the current logins
		 * can't use time comparison because there may be a small
		 * discrepancy since login calls time() twice
		 */
		for (w = pn->whead; doit && w != NULL; w = w->next)
			if (w->info == LOGGEDIN &&
			    strcmp(w->tty, ut->ut_line) == 0)
				doit = 0;
	}
	if (ut != NULL && doit) {
		w = walloc(pn);
		w->info = LASTLOG;
		strcpy(w->tty, ut->ut_line);
		strcpy(w->host, ut->ut_host);
		w->loginat = ut->ut_tv.tv_sec;
	}
	endutxent();
}

void
enter_where(struct utmpx *ut, PERSON *pn)
{
	WHERE *w;

	w = walloc(pn);
	w->info = LOGGEDIN;
	strcpy(w->tty, ut->ut_line);
	strcpy(w->host, ut->ut_host);
	w->loginat = ut->ut_tv.tv_sec;
	find_idle_and_ttywrite(w);
}

PERSON *
enter_person(struct passwd *pw)
{
	DBT data, key;
	PERSON *pn;

	if (db == NULL &&
	    (db = dbopen(NULL, O_RDWR, 0, DB_BTREE, NULL)) == NULL)
		err(1, NULL);

	key.data = pw->pw_name;
	key.size = strlen(pw->pw_name);

	switch ((*db->get)(db, &key, &data, 0)) {
	case 0:
		memmove(&pn, data.data, sizeof pn);
		return (pn);
	default:
	case -1:
		err(1, "db get");
		/* NOTREACHED */
	case 1:
		++entries;
		pn = palloc();
		userinfo(pn, pw);
		pn->whead = NULL;

		data.size = sizeof(PERSON *);
		data.data = &pn;
		if ((*db->put)(db, &key, &data, 0))
			err(1, "db put");
		return (pn);
	}
}

PERSON *
find_person(char *name)
{
	struct passwd *pw;

	DBT data, key;
	PERSON *p;

	if (!db)
		return(NULL);

	if ((pw = getpwnam(name)) && hide(pw))
		return(NULL);

	key.data = name;
	key.size = strlen(name);

	if ((*db->get)(db, &key, &data, 0))
		return (NULL);
	memmove(&p, data.data, sizeof p);
	return (p);
}

PERSON *
palloc(void)
{
	PERSON *p;

	if ((p = malloc(sizeof(PERSON))) == NULL)
		err(1, NULL);
	return(p);
}

static WHERE *
walloc(PERSON *pn)
{
	WHERE *w;

	if ((w = malloc(sizeof(WHERE))) == NULL)
		err(1, NULL);
	if (pn->whead == NULL)
		pn->whead = pn->wtail = w;
	else {
		pn->wtail->next = w;
		pn->wtail = w;
	}
	w->next = NULL;
	return(w);
}

char *
prphone(char *num)
{
	char *p;
	int len;
	static char pbuf[20];

	/* don't touch anything if the user has their own formatting */
	for (p = num; *p; ++p)
		if (!isdigit(*p))
			return(num);
	len = p - num;
	p = pbuf;
	switch(len) {
	case 11:			/* +0-123-456-7890 */
		*p++ = '+';
		*p++ = *num++;
		*p++ = '-';
		/* FALLTHROUGH */
	case 10:			/* 012-345-6789 */
		*p++ = *num++;
		*p++ = *num++;
		*p++ = *num++;
		*p++ = '-';
		/* FALLTHROUGH */
	case 7:				/* 012-3456 */
		*p++ = *num++;
		*p++ = *num++;
		*p++ = *num++;
		break;
	case 5:				/* x0-1234 */
	case 4:				/* x1234 */
		*p++ = 'x';
		*p++ = *num++;
		break;
	default:
		return(num);
	}
	if (len != 4) {
	    *p++ = '-';
	    *p++ = *num++;
	}
	*p++ = *num++;
	*p++ = *num++;
	*p++ = *num++;
	*p = '\0';
	return(pbuf);
}

static void
find_idle_and_ttywrite(WHERE *w)
{
	struct stat sb;
	time_t touched;
	int error;

	(void)snprintf(tbuf, sizeof(tbuf), "%s/%s", _PATH_DEV, w->tty);

	error = stat(tbuf, &sb);
	if (error < 0 && errno == ENOENT) {
		/*
		 * The terminal listed is not actually a terminal (i.e.,
		 * ":0").  This is a failure, so we'll skip printing
		 * out the idle time, which is non-ideal but better
		 * than a bogus warning and idle time.
		 */
		w->idletime = -1;
		return;
	} else if (error < 0) {
		warn("%s", tbuf);
		w->idletime = -1;
		return;
	}
	touched = sb.st_atime;
	if (touched < w->loginat) {
		/* tty untouched since before login */
		touched = w->loginat;
	}
	w->idletime = now < touched ? 0 : now - touched;

#define	TALKABLE	0220		/* tty is writable if 220 mode */
	w->writable = ((sb.st_mode & TALKABLE) == TALKABLE);
}

static void
userinfo(PERSON *pn, struct passwd *pw)
{
	char *p, *t;
	char *bp, name[1024];
	struct stat sb;

	pn->realname = pn->office = pn->officephone = pn->homephone = NULL;

	pn->uid = pw->pw_uid;
	if ((pn->name = strdup(pw->pw_name)) == NULL)
		err(1, "strdup failed");
	if ((pn->dir = strdup(pw->pw_dir)) == NULL)
		err(1, "strdup failed");
	if ((pn->shell = strdup(pw->pw_shell)) == NULL)
		err(1, "strdup failed");

	/* why do we skip asterisks!?!? */
	(void)strncpy(bp = tbuf, pw->pw_gecos, sizeof(tbuf));
	tbuf[sizeof(tbuf) - 1] = '\0';
	if (*bp == '*')
		++bp;

	/* ampersands get replaced by the login name */
	if (!(p = strsep(&bp, ",")))
		return;
	for (t = name; t < &name[sizeof(name) - 1] && (*t = *p) != '\0'; ++p) {
		if (*t == '&') {
			(void)strncpy(t, pw->pw_name, 
			    sizeof(name) - (t - name));
			name[sizeof(name) - 1] = '\0';
			if (islower(*t))
				*t = toupper(*t);
			while (t < &name[sizeof(name) - 1] && *++t)
				continue;
		} else {
			++t;
		}
	}
	*t = '\0';
	if ((pn->realname = strdup(name)) == NULL)
		err(1, "strdup failed");
	pn->office = ((p = strsep(&bp, ",")) && *p) ?
	    strdup(p) : NULL;
	pn->officephone = ((p = strsep(&bp, ",")) && *p) ?
	    strdup(p) : NULL;
	pn->homephone = ((p = strsep(&bp, ",")) && *p) ?
	    strdup(p) : NULL;
	(void)snprintf(tbuf, sizeof(tbuf), "%s/%s", _PATH_MAILDIR, pw->pw_name);
	pn->mailrecv = -1;		/* -1 == not_valid */
	if (stat(tbuf, &sb) < 0) {
		if (errno != ENOENT) {
			warn("%s", tbuf);
			return;
		}
	} else if (sb.st_size != 0) {
		pn->mailrecv = sb.st_mtime;
		pn->mailread = sb.st_atime;
	}
}

/*
 * Is this user hiding from finger?
 * If ~<user>/.nofinger exists, return 1 (hide), else return 0 (nohide).
 * Nobody can hide from root.
 */

int
hide(struct passwd *pw)
{
	struct stat st;
	char buf[MAXPATHLEN];

	if (invoker_root || !pw->pw_dir)
		return 0;

	snprintf(buf, sizeof(buf), "%s/%s", pw->pw_dir, _PATH_NOFINGER);

	if (stat(buf, &st) == 0)
		return 1;

	return 0;
}
