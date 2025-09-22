/*	$OpenBSD: user.c,v 1.22 2020/04/16 17:51:56 millert Exp $	*/

/* Copyright 1988,1990,1993,1994 by Paul Vixie
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

#include <sys/types.h>

#include <bitstring.h>		/* for structs.h */
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>		/* for structs.h */

#include "macros.h"
#include "structs.h"
#include "funcs.h"
#include "globals.h"

void
free_user(user *u)
{
	entry *e;

	while ((e = SLIST_FIRST(&u->crontab))) {
		SLIST_REMOVE_HEAD(&u->crontab, entries);
		job_remove(e, u);
		free_entry(e);
	}
	free(u->name);
	free(u);
}

static int ParseErrorCount;
static const char *CrontabFilename;

static void
parse_error(const char *msg)
{
	ParseErrorCount++;
	syslog(LOG_ERR, "(CRON) %s:%d (%s)", CrontabFilename, LineNumber, msg);
}

user *
load_user(FILE *file, struct passwd *pw, const char *name)
{
	char envstr[MAX_ENVSTR];
	user *u;
	entry *e;
	int status, save_errno;
	char **envp = NULL, **tenvp;

	CrontabFilename = name;
	LineNumber = 0;

	/* file is open.  build user entry, then read the crontab file.
	 */
	if ((u = malloc(sizeof(user))) == NULL)
		goto done;
	if ((u->name = strdup(name)) == NULL) {
		save_errno = errno;
		free(u);
		u = NULL;
		errno = save_errno;
		goto done;
	}
	SLIST_INIT(&u->crontab);

	/* init environment.  this will be copied/augmented for each entry.
	 */
	if ((envp = env_init()) == NULL) {
		save_errno = errno;
		free_user(u);
		u = NULL;
		errno = save_errno;
		goto done;
	}

	/* load the crontab
	 */
	ParseErrorCount = 0;
	while ((status = load_env(envstr, file)) >= 0) {
		switch (status) {
		case FALSE:
			/* Not an env variable, parse as crontab entry. */
			e = load_entry(file, parse_error, pw, envp);
			if (e == NULL) {
				/* Parse error, ignore for non-root entries */
				if (pw != NULL) {
					save_errno = errno;
					free_user(u);
					u = NULL;
					errno = save_errno;
					goto done;
				}
			} else {
				SLIST_INSERT_HEAD(&u->crontab, e, entries);
			}
			break;
		case TRUE:
			if ((tenvp = env_set(envp, envstr)) == NULL) {
				save_errno = errno;
				free_user(u);
				u = NULL;
				errno = save_errno;
				goto done;
			}
			envp = tenvp;
			break;
		}
	}

 done:
	if (envp != NULL)
		env_free(envp);
	return (u);
}
